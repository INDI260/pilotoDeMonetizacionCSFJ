#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
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
  int quantity;
  double unitCost;
  
  double getTotalCost() const {
    return quantity * unitCost;
  }
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

std::string formatCurrencyWithGrouping(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << value;
  std::string number = out.str();
  const auto dotPos = number.find('.');
  std::string integerPart = dotPos == std::string::npos ? number : number.substr(0, dotPos);
  const std::string decimalPart = dotPos == std::string::npos ? std::string{} : number.substr(dotPos);

  std::vector<std::string> groups;
  for (std::ptrdiff_t end = static_cast<std::ptrdiff_t>(integerPart.size()); end > 0; end -= 3) {
    const std::ptrdiff_t start = std::max<std::ptrdiff_t>(0, end - 3);
    groups.emplace_back(integerPart.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
  }
  std::reverse(groups.begin(), groups.end());

  if (groups.empty()) {
    groups.emplace_back("0");
  }

  std::string grouped = groups[0];
  for (size_t index = 1; index < groups.size(); ++index) {
    grouped += (index == 1 && groups.size() > 2) ? '\'' : ',';
    grouped += groups[index];
  }

  return grouped + decimalPart;
}

std::string normalizeCostInput(const std::string& raw) {
  std::string normalized;
  normalized.reserve(raw.size());
  for (char ch : raw) {
    if (ch == '\'' || ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
      continue;
    }
    normalized.push_back(ch);
  }
  return normalized;
}

std::string loadTemplateFile(const std::string& filename) {
  const std::filesystem::path templatePath = std::filesystem::path("templates") / filename;
  std::ifstream file(templatePath, std::ios::binary);
  if (!file) {
    throw std::runtime_error("No se pudo abrir la plantilla: " + templatePath.string());
  }
  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

const std::string& indexTemplate() {
  static const std::string templateContent = loadTemplateFile("index.html");
  return templateContent;
}

const std::string& editTemplate() {
  static const std::string templateContent = loadTemplateFile("edit.html");
  return templateContent;
}

void replaceAll(std::string& target, const std::string& placeholder, const std::string& value) {
  size_t position = 0U;
  while ((position = target.find(placeholder, position)) != std::string::npos) {
    target.replace(position, placeholder.size(), value);
    position += value.size();
  }
}

std::string renderTemplateError(const std::string& message) {
  return std::string{"<html><body><h1>Error interno</h1><p>"} + escapeHtml(message) + "</p></body></html>";
}

std::string escapeCsv(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (const char ch : value) {
    if (ch == '"') {
      out << "\"\"";
    } else {
      out << ch;
    }
  }
  out << '"';
  return out.str();
}

std::string loadStaticFile(const std::string& filename) {
  const std::filesystem::path staticPath = std::filesystem::path("static") / filename;
  std::ifstream file(staticPath, std::ios::binary);
  if (!file) {
    throw std::runtime_error("No se pudo abrir el activo estático: " + staticPath.string());
  }
  std::ostringstream content;
  content << file.rdbuf();
  return content.str();
}

const std::string& stylesAsset() {
  static const std::string content = loadStaticFile("styles.css");
  return content;
}

const std::string& formatterAsset() {
  static const std::string content = loadStaticFile("formatter.js");
  return content;
}

std::string renderItemsTable() {
  std::ostringstream rows;
  double totalCost = 0.0;
  {
    std::lock_guard<std::mutex> guard(g_itemsMutex);
    for (size_t index = 0; index < g_items.size(); ++index) {
      const Item& item = g_items[index];
      const double itemTotal = item.getTotalCost();
      rows << "      <tr><td>" << (index + 1) << "</td><td>" << escapeHtml(item.name)
           << "</td><td>" << item.quantity
           << "</td><td>" << formatCurrencyWithGrouping(item.unitCost)
           << "</td><td>" << formatCurrencyWithGrouping(itemTotal)
           << "</td><td class=\"actions\"><form class=\"action-form\" method=\"GET\" action=\"/edit\">"
           << "<input type=\"hidden\" name=\"index\" value=\"" << index << "\">"
           << "<button class=\"action-button\" type=\"submit\">Editar</button></form></td></tr>\n";
      totalCost += itemTotal;
    }
  }

  std::string page;
  try {
    page = indexTemplate();
  } catch (const std::exception& ex) {
    return renderTemplateError(ex.what());
  }

  replaceAll(page, "{{items_rows}}", rows.str());
  replaceAll(page, "{{total_cost}}", formatCurrencyWithGrouping(totalCost));

  return page;
}

std::string renderEditPage(size_t index, const Item& item) {
  std::string page;
  try {
    page = editTemplate();
  } catch (const std::exception& ex) {
    return renderTemplateError(ex.what());
  }

  replaceAll(page, "{{item_index}}", std::to_string(index));
  replaceAll(page, "{{item_name}}", escapeHtml(item.name));
  replaceAll(page, "{{item_quantity}}", std::to_string(item.quantity));
  replaceAll(page, "{{item_cost}}", formatCurrency(item.unitCost));

  return page;
}

void sendResponse(SOCKET client,
                  const std::string& statusLine,
                  const std::string& contentType,
                  const std::string& body,
                  const std::string& extraHeaders = std::string{}) {
  std::ostringstream response;
  response << statusLine << "\r\n"
           << "Content-Type: " << contentType << "\r\n";
  if (!extraHeaders.empty()) {
    response << extraHeaders;
  }
  response << "Content-Length: " << body.size() << "\r\n"
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

bool tryServeStaticAsset(const std::string& path, SOCKET client) {
  try {
    if (path == "/static/styles.css") {
      sendResponse(client, "HTTP/1.1 200 OK", "text/css; charset=utf-8", stylesAsset());
      return true;
    }
    if (path == "/static/formatter.js") {
      sendResponse(client, "HTTP/1.1 200 OK", "application/javascript; charset=utf-8", formatterAsset());
      return true;
    }
  } catch (const std::exception& ex) {
    const auto errorPage = renderTemplateError(ex.what());
    sendResponse(client, "HTTP/1.1 500 Internal Server Error", "text/html; charset=utf-8", errorPage);
    return true;
  }
  return false;
}

void handlePostSubmit(const std::string& body, SOCKET client) {
  const auto formValues = parseFormBody(body);
  
  // Get item name from dropdown or custom field
  std::string itemName;
  const auto selectIt = formValues.find("itemNameSelect");
  if (selectIt != formValues.end() && !selectIt->second.empty()) {
    if (selectIt->second == "Otro...") {
      const auto customIt = formValues.find("itemName");
      if (customIt != formValues.end()) {
        itemName = customIt->second;
      }
    } else {
      itemName = selectIt->second;
    }
  }
  
  const auto costIt = formValues.find("itemCost");
  const auto quantityIt = formValues.find("itemQuantity");
  if (itemName.empty() || costIt == formValues.end() || quantityIt == formValues.end()) {
    const std::string message = "Faltan campos requeridos.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
    return;
  }
  
  int quantity = 0;
  try {
    quantity = std::stoi(quantityIt->second);
    if (quantity < 1) {
      throw std::invalid_argument("quantity must be positive");
    }
  } catch (const std::exception&) {
    const std::string message = "Cantidad inválida. Debe ser un número entero positivo.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
    return;
  }
  
  try {
    const std::string normalizedCost = normalizeCostInput(costIt->second);
    if (normalizedCost.empty()) {
      throw std::invalid_argument("empty");
    }
    const double cost = std::stod(normalizedCost);
    if (cost < 0.0) {
      throw std::invalid_argument("negative");
    }
    {
      std::lock_guard<std::mutex> guard(g_itemsMutex);
      g_items.push_back({itemName, quantity, cost});
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
  
  // Get item name from dropdown or custom field
  std::string itemName;
  const auto selectIt = formValues.find("itemNameSelect");
  if (selectIt != formValues.end() && !selectIt->second.empty()) {
    if (selectIt->second == "Otro...") {
      const auto customIt = formValues.find("itemName");
      if (customIt != formValues.end()) {
        itemName = customIt->second;
      }
    } else {
      itemName = selectIt->second;
    }
  }
  
  const auto costIt = formValues.find("itemCost");
  const auto quantityIt = formValues.find("itemQuantity");
  if (indexIt == formValues.end() || itemName.empty() || costIt == formValues.end() || quantityIt == formValues.end()) {
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
  
  int quantity = 0;
  try {
    quantity = std::stoi(quantityIt->second);
    if (quantity < 1) {
      throw std::invalid_argument("quantity must be positive");
    }
  } catch (const std::exception&) {
    const std::string message = "Cantidad inválida. Debe ser un número entero positivo.";
    sendResponse(client, "HTTP/1.1 400 Bad Request", "text/plain; charset=utf-8", message);
    return;
  }

  try {
    const std::string normalizedCost = normalizeCostInput(costIt->second);
    if (normalizedCost.empty()) {
      throw std::invalid_argument("empty");
    }
    const double cost = std::stod(normalizedCost);
    if (cost < 0.0) {
      throw std::invalid_argument("negative");
    }

    bool itemMissing = false;
    {
      std::lock_guard<std::mutex> guard(g_itemsMutex);
      if (itemIndex >= g_items.size()) {
        itemMissing = true;
      } else {
        g_items[itemIndex] = {itemName, quantity, cost};
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

  if (method == "GET" && tryServeStaticAsset(path, client)) {
    return;
  }

  if (method == "GET" && (path == "/" || path == "/index.html")) {
    const auto html = renderItemsTable();
    sendResponse(client, "HTTP/1.1 200 OK", "text/html; charset=utf-8", html);
  } else if (method == "GET" && path == "/export") {
    std::ostringstream csv;
    csv << "Nombre,Cantidad,Costo Unitario,Total\r\n";

    double totalCost = 0.0;
    {
      std::lock_guard<std::mutex> guard(g_itemsMutex);
      for (const Item& item : g_items) {
        const double itemTotal = item.getTotalCost();
        csv << escapeCsv(item.name) << ',' 
            << item.quantity << ','
            << escapeCsv(formatCurrency(item.unitCost)) << ','
            << escapeCsv(formatCurrency(itemTotal)) << "\r\n";
        totalCost += itemTotal;
      }
    }

    csv << escapeCsv("Total") << ",,," << escapeCsv(formatCurrency(totalCost)) << "\r\n";

    const auto csvBody = csv.str();
    const std::string disposition = "Content-Disposition: attachment; filename=\"items.csv\"\r\n";
    sendResponse(client, "HTTP/1.1 200 OK", "text/csv; charset=utf-8", csvBody, disposition);
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
