#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
  #define _WINSOCK_DEPRECATED_NO_WARNINGS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int SOCKET;
  const int INVALID_SOCKET = -1;
  const int SOCKET_ERROR = -1;
#endif

namespace {

constexpr unsigned short kServerPort = 8080;
constexpr int kSocketBufferSize = 4096;

struct Item {
  std::string name;
  double cost;
};

std::vector<Item> g_items;
std::mutex g_itemsMutex;

#ifdef _WIN32
void closeSocket(SOCKET socket) {
  shutdown(socket, SD_BOTH);
  closesocket(socket);
}
#else
void closeSocket(SOCKET socket) {
  shutdown(socket, SHUT_RDWR);
  close(socket);
}
#endif

std::string trim(const std::string& input) {
  const auto first = std::find_if_not(input.begin(), input.end(), [](unsigned char ch) { return std::isspace(ch); });
  const auto last = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::unordered_map<std::string, std::string> parseHeaders(const std::string& headerBlock) {
  std::unordered_map<std::string, std::string> headers;
  size_t cursor = 0U;
  while (cursor < headerBlock.size()) {
    const auto lineEnd = headerBlock.find("\r\n", cursor);
    const auto length = (lineEnd == std::string::npos) ? headerBlock.size() - cursor : lineEnd - cursor;
    const auto line = headerBlock.substr(cursor, length);
    cursor = (lineEnd == std::string::npos) ? headerBlock.size() : lineEnd + 2;
    if (line.empty()) {
      continue;
    }
    const auto colonPos = line.find(':');
    if (colonPos == std::string::npos) {
      continue;
    }
    const auto key = toLower(trim(line.substr(0, colonPos)));
    const auto value = trim(line.substr(colonPos + 1));
    headers[key] = value;
  }
  return headers;
}

std::string urlDecode(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    const char current = value[index];
    if (current == '+') {
      result.push_back(' ');
    } else if (current == '%' && index + 2 < value.size()) {
      const auto hex = value.substr(index + 1, 2);
      try {
        const auto decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
        result.push_back(decoded);
        index += 2;
      } catch (const std::exception&) {
        result.push_back(current);
      }
    } else {
      result.push_back(current);
    }
  }
  return result;
}

std::unordered_map<std::string, std::string> parseFormBody(const std::string& body) {
  std::unordered_map<std::string, std::string> formValues;
  size_t start = 0U;
  while (start <= body.size()) {
    const auto amp = body.find('&', start);
    const auto token = body.substr(start, (amp == std::string::npos) ? std::string::npos : amp - start);
    const auto equal = token.find('=');
    if (equal != std::string::npos) {
      const auto key = urlDecode(token.substr(0, equal));
      const auto value = urlDecode(token.substr(equal + 1));
      formValues[key] = value;
    }
    if (amp == std::string::npos) {
      break;
    }
    start = amp + 1;
  }
  return formValues;
}

std::string escapeHtml(const std::string& value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&':
        sanitized += "&amp;";
        break;
      case '<':
        sanitized += "&lt;";
        break;
      case '>':
        sanitized += "&gt;";
        break;
      case '"':
        sanitized += "&quot;";
        break;
      case '\'':
        sanitized += "&#39;";
        break;
      default:
        sanitized.push_back(ch);
        break;
    }
  }
  return sanitized;
}

std::string formatCurrency(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << value;
  return out.str();
}

std::string renderCommonStyles() {
  return
      "  <style>body{font-family:Segoe UI,Arial,sans-serif;margin:2rem;background:#f5f7fb;color:#1f2d3d;}"
      "h1{margin-bottom:0.5rem;}"
      ".lead{margin-bottom:1.5rem;color:#334155;}"
      ".entry-form{background:#fff;padding:1rem;border-radius:8px;box-shadow:0 2px 6px rgba(15,23,42,0.15);max-width:520px;margin-bottom:2rem;}"
      ".entry-form label{display:block;margin-bottom:0.5rem;font-weight:600;}"
      ".entry-form input[type=text],.entry-form input[type=number]{width:100%;padding:0.5rem;margin-bottom:0.75rem;border:1px solid #cbd5f5;border-radius:6px;}"
      ".primary-button{padding:0.6rem 1.2rem;background:#2563eb;border:none;border-radius:6px;color:#fff;font-weight:600;cursor:pointer;}"
      ".primary-button:hover{background:#1e40af;}"
      "table{margin-top:2rem;width:100%;border-collapse:collapse;background:#fff;box-shadow:0 2px 6px rgba(15,23,42,0.1);border-radius:8px;overflow:hidden;}"
      "th,td{padding:0.75rem;border-bottom:1px solid #d9e3f5;text-align:left;}"
      "th{background:#e2e8f8;}"
      "tbody tr:nth-child(odd){background:#f8fbff;}"
      "tfoot td{font-weight:700;}"
      ".actions{width:1%;white-space:nowrap;}"
      ".action-form{display:inline;}"
      ".action-button{padding:0.35rem 0.9rem;background:#0ea5e9;border:none;border-radius:6px;color:#fff;font-weight:600;cursor:pointer;}"
      ".action-button:hover{background:#0284c7;}"
      ".link-button{display:inline-block;margin-top:1rem;color:#2563eb;font-weight:600;text-decoration:none;}"
      ".link-button:hover{text-decoration:underline;}"
      "</style>\n";
}

std::string renderItemsTable() {
  std::ostringstream stream;
  stream << "<!DOCTYPE html>\n"
         << "<html lang=\"es\">\n"
         << "<head>\n"
         << "  <meta charset=\"utf-8\">\n"
         << "  <title>Piloto de Monetización CSFJ</title>\n"
         << renderCommonStyles()
         << "</head>\n"
         << "<body>\n"
         << "  <h1>Piloto de Monetización CSFJ</h1>\n"
         << "  <p class=\"lead\">Registra los items y sus costos asociados. Toda la información se mantiene en memoria mientras el servidor está activo.</p>\n"
         << "  <form class=\"entry-form\" method=\"POST\" action=\"/submit\">\n"
         << "    <label for=\"itemName\">Nombre del item</label>\n"
         << "    <input id=\"itemName\" name=\"itemName\" type=\"text\" required maxlength=\"120\">\n"
         << "    <label for=\"itemCost\">Costo</label>\n"
         << "    <input id=\"itemCost\" name=\"itemCost\" type=\"number\" step=\"0.01\" min=\"0\" required>\n"
         << "    <button class=\"primary-button\" type=\"submit\">Agregar</button>\n"
         << "  </form>\n";

  double totalCost = 0.0;
  stream << "  <table>\n"
         << "    <thead><tr><th>#</th><th>Item</th><th>Costo</th><th>Acciones</th></tr></thead>\n"
         << "    <tbody>\n";
  {
    std::lock_guard<std::mutex> guard(g_itemsMutex);
    for (size_t index = 0; index < g_items.size(); ++index) {
      const Item& item = g_items[index];
      stream << "      <tr><td>" << (index + 1) << "</td><td>" << escapeHtml(item.name)
             << "</td><td>" << formatCurrency(item.cost)
             << "</td><td class=\"actions\"><form class=\"action-form\" method=\"GET\" action=\"/edit\">"
             << "<input type=\"hidden\" name=\"index\" value=\"" << index << "\">"
             << "<button class=\"action-button\" type=\"submit\">Editar</button></form></td></tr>\n";
      totalCost += item.cost;
    }
  }
  stream << "    </tbody>\n"
         << "    <tfoot><tr><td colspan=\"3\">Total</td><td>" << formatCurrency(totalCost) << "</td></tr></tfoot>\n"
         << "  </table>\n"
         << "</body>\n"
         << "</html>\n";
  return stream.str();
}

std::string renderEditPage(size_t index, const Item& item) {
  std::ostringstream stream;
  stream << "<!DOCTYPE html>\n"
         << "<html lang=\"es\">\n"
         << "<head>\n"
         << "  <meta charset=\"utf-8\">\n"
         << "  <title>Piloto de Monetización CSFJ — Editar item</title>\n"
         << renderCommonStyles()
         << "</head>\n"
         << "<body>\n"
         << "  <h1>Editar item</h1>\n"
         << "  <p class=\"lead\">Actualiza la información del item seleccionado y guarda los cambios para que se reflejen en el listado.</p>\n"
         << "  <form class=\"entry-form\" method=\"POST\" action=\"/update\">\n"
         << "    <input type=\"hidden\" name=\"itemIndex\" value=\"" << index << "\">\n"
         << "    <label for=\"itemName\">Nombre del item</label>\n"
         << "    <input id=\"itemName\" name=\"itemName\" type=\"text\" required maxlength=\"120\" value=\"" << escapeHtml(item.name) << "\">\n"
         << "    <label for=\"itemCost\">Costo</label>\n"
         << "    <input id=\"itemCost\" name=\"itemCost\" type=\"number\" step=\"0.01\" min=\"0\" required value=\"" << formatCurrency(item.cost) << "\">\n"
         << "    <button class=\"primary-button\" type=\"submit\">Guardar cambios</button>\n"
         << "  </form>\n"
         << "  <a class=\"link-button\" href=\"/\">Cancelar y volver al listado</a>\n"
         << "</body>\n"
         << "</html>\n";
  return stream.str();
}

void sendResponse(SOCKET client, const std::string& statusLine, const std::string& contentType, const std::string& body) {
  std::ostringstream response;
  response << statusLine << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
  const auto payload = response.str();
  send(client, payload.data(), static_cast<int>(payload.size()), 0);
}

void sendRedirect(SOCKET client, const std::string& location) {
  std::ostringstream response;
  response << "HTTP/1.1 303 See Other\r\n"
           << "Location: " << location << "\r\n"
           << "Content-Length: 0\r\n"
           << "Connection: close\r\n\r\n";
  const auto payload = response.str();
  send(client, payload.data(), static_cast<int>(payload.size()), 0);
}

void handlePostSubmit(const std::string& body, SOCKET client) {
  const auto formValues = parseFormBody(body);
  const auto nameIt = formValues.find("itemName");
  const auto costIt = formValues.find("itemCost");
  if (nameIt == formValues.end() || costIt == formValues.end()) {
    const std::string message = "Faltan campos requeridos.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
    return;
  }
  try {
    const double cost = std::stod(costIt->second);
    if (cost < 0.0) {
      throw std::invalid_argument("negative");
    }
    {
      std::lock_guard<std::mutex> guard(g_itemsMutex);
      g_items.push_back({nameIt->second, cost});
    }
    sendRedirect(client, "/");
  } catch (const std::exception&) {
    const std::string message = "Costo inválido. Usa un número positivo.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
  }
}

void handlePostUpdate(const std::string& body, SOCKET client) {
  const auto formValues = parseFormBody(body);
  const auto indexIt = formValues.find("itemIndex");
  const auto nameIt = formValues.find("itemName");
  const auto costIt = formValues.find("itemCost");
  if (indexIt == formValues.end() || nameIt == formValues.end() || costIt == formValues.end()) {
    const std::string message = "Faltan campos requeridos.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
    return;
  }

  size_t itemIndex = 0U;
  try {
    itemIndex = static_cast<size_t>(std::stoul(indexIt->second));
  } catch (const std::exception&) {
    const std::string message = "Índice de item inválido.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
    return;
  }

  try {
    const double cost = std::stod(costIt->second);
    if (cost < 0.0) {
      throw std::invalid_argument("negative");
    }

    bool itemMissing = false;
    {
      std::lock_guard<std::mutex> guard(g_itemsMutex);
      if (itemIndex >= g_items.size()) {
        itemMissing = true;
      } else {
        g_items[itemIndex] = {nameIt->second, cost};
      }
    }

    if (itemMissing) {
      const std::string message = "El item solicitado no existe.";
      sendResponse(client, "HTTP/1.1 404 Not Found", "text/plain; charset=utf-8", message);
      return;
    }

    sendRedirect(client, "/");
  } catch (const std::exception&) {
    const std::string message = "Costo inválido. Usa un número positivo.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
  }
}

void handleClient(SOCKET client) {
  std::string request;
  request.reserve(2048);

  int expectedContentLength = 0;
  bool headersParsed = false;
  size_t headerEndPos = std::string::npos;

  char buffer[kSocketBufferSize];
  while (true) {
    const int bytesReceived = recv(client, buffer, sizeof(buffer), 0);
    if (bytesReceived <= 0) {
      break;
    }
    request.append(buffer, bytesReceived);

    if (!headersParsed) {
      headerEndPos = request.find("\r\n\r\n");
      if (headerEndPos != std::string::npos) {
        const std::string headersPart = request.substr(0, headerEndPos);
        const auto headers = parseHeaders(headersPart);
        const auto contentLengthIt = headers.find("content-length");
        if (contentLengthIt != headers.end()) {
          expectedContentLength = std::stoi(contentLengthIt->second);
        }
        headersParsed = true;
      }
    }

    if (headersParsed) {
      const size_t currentBodySize = (headerEndPos == std::string::npos) ? 0 : request.size() - (headerEndPos + 4);
      if (static_cast<int>(currentBodySize) >= expectedContentLength) {
        break;
      }
    }
  }

  if (request.empty()) {
    return;
  }

  const auto requestLineEnd = request.find("\r\n");
  if (requestLineEnd == std::string::npos) {
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", "Petición inválida");
    return;
  }

  const auto requestLine = request.substr(0, requestLineEnd);
  std::istringstream requestLineStream(requestLine);
  std::string method;
  std::string rawPath;
  requestLineStream >> method >> rawPath;

  if (method.empty() || rawPath.empty()) {
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", "Petición inválida");
    return;
  }

  std::string path = rawPath;
  std::string queryString;
  const auto queryPos = rawPath.find('?');
  if (queryPos != std::string::npos) {
    path = rawPath.substr(0, queryPos);
    queryString = rawPath.substr(queryPos + 1);
  }

  const size_t headersBlockStart = requestLineEnd + 2;
  const std::string headersBlock = (headerEndPos == std::string::npos || headerEndPos <= headersBlockStart)
                                       ? std::string{}
                                       : request.substr(headersBlockStart, headerEndPos - headersBlockStart);
  const auto headers = parseHeaders(headersBlock);
  std::string body;
  if (headerEndPos != std::string::npos) {
    const size_t bodyStart = headerEndPos + 4;
    if (bodyStart < request.size()) {
      body = request.substr(bodyStart);
    }
  }

  if (method == "GET" && (path == "/" || path == "/index.html")) {
    const auto html = renderItemsTable();
    sendResponse(client, "HTTP/1.1 200 OK", "text/html; charset=utf-8", html);
  } else if (method == "GET" && path == "/edit") {
    const auto queryValues = parseFormBody(queryString);
    const auto indexIt = queryValues.find("index");
    if (indexIt == queryValues.end()) {
      sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", "Índice de item requerido");
      return;
    }
    size_t itemIndex = 0U;
    try {
      itemIndex = static_cast<size_t>(std::stoul(indexIt->second));
    } catch (const std::exception&) {
      sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", "Índice de item inválido");
      return;
    }

    Item itemSnapshot;
    bool itemMissing = false;
    {
      std::lock_guard<std::mutex> guard(g_itemsMutex);
      if (itemIndex >= g_items.size()) {
        itemMissing = true;
      } else {
        itemSnapshot = g_items[itemIndex];
      }
    }

    if (itemMissing) {
      sendResponse(client, "HTTP/1.1 404 Not Found", "text/plain; charset=utf-8", "El item solicitado no existe");
      return;
    }

    const auto html = renderEditPage(itemIndex, itemSnapshot);
    sendResponse(client, "HTTP/1.1 200 OK", "text/html; charset=utf-8", html);
  } else if (method == "POST" && path == "/submit") {
    const auto contentTypeIt = headers.find("content-type");
    if (contentTypeIt == headers.end() || contentTypeIt->second.find("application/x-www-form-urlencoded") == std::string::npos) {
      sendResponse(client, "HTTP/1.1 415 Unsupported Media Type", "text/plain; charset=utf-8", "Contenido no soportado");
      return;
    }
    handlePostSubmit(body, client);
  } else if (method == "POST" && path == "/update") {
    const auto contentTypeIt = headers.find("content-type");
    if (contentTypeIt == headers.end() || contentTypeIt->second.find("application/x-www-form-urlencoded") == std::string::npos) {
      sendResponse(client, "HTTP/1.1 415 Unsupported Media Type", "text/plain; charset=utf-8", "Contenido no soportado");
      return;
    }
    handlePostUpdate(body, client);
  } else {
    const std::string notFoundHtml = "<html><body><h1>404 - Recurso no encontrado</h1></body></html>";
    sendResponse(client, "HTTP/1.1 404 Not Found", "text/html; charset=utf-8", notFoundHtml);
  }
}

class SocketEnvironment {
public:
  SocketEnvironment() {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("No se pudo inicializar Winsock");
    }
#endif
  }

  ~SocketEnvironment() {
#ifdef _WIN32
    WSACleanup();
#endif
  }
};

void runServer(unsigned short port) {
  SocketEnvironment env;

  SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (serverSocket == INVALID_SOCKET) {
    throw std::runtime_error("No se pudo crear el socket del servidor");
  }

  int reuse = 1;
#ifdef _WIN32
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

  sockaddr_in serverAddress{};
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddress.sin_port = htons(port);

  if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
    closeSocket(serverSocket);
    throw std::runtime_error("No se pudo asociar el socket al puerto");
  }

  if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
    closeSocket(serverSocket);
    throw std::runtime_error("No se pudo iniciar la escucha del servidor");
  }

  std::cout << "Servidor iniciado en http://localhost:" << port << std::endl;

  while (true) {
    SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
    if (clientSocket == INVALID_SOCKET) {
      continue;
    }
    handleClient(clientSocket);
    closeSocket(clientSocket);
  }

  closeSocket(serverSocket);
}

}  // namespace

int main() {
  try {
    runServer(kServerPort);
  } catch (const std::exception& ex) {
    std::cerr << "Error fatal: " << ex.what() << std::endl;
    return 1;
  }
  return 0;
}
