# Piloto de Monetización CSFJ

Aplicación web para gestionar y calcular costos de items relacionados con actividades de monetización de la Convocatoria San Francisco Javier (CSFJ). Construida completamente en C++17 con un servidor HTTP integrado, sin dependencias externas.

## Tecnologías

- **Backend:** C++17 con servidor HTTP nativo (sockets)
- **Frontend:** HTML5, CSS3, JavaScript vanilla
- **Build System:** CMake 3.16+
- **Compatibilidad:** Windows (Winsock2), Linux/macOS (POSIX sockets)

## Características Principales

- **Agregar items** con lista desplegable de 10 categorías predefinidas:
  - Hora docente, Viáticos, Transporte, Material didáctico, Refrigerio
  - Alquiler de espacio, Equipamiento, Servicios profesionales, Publicidad, Certificaciones
  - Opción "Otro..." para items personalizados
- **Visualizar tabla** con cálculo automático de totales por item y total general
- **Editar items** existentes
- **Exportar a CSV** para análisis en Excel u otras herramientas
- **Formateo de moneda** en tiempo real con separadores de miles

## Estructura del Proyecto

```text
pilotoDeMonetizacionCSFJ/
├── CMakeLists.txt          # Configuración de compilación CMake
├── README.md               # Este archivo
├── src/
│   └── main.cpp            # Servidor HTTP y lógica principal (~750 líneas)
├── templates/
│   ├── index.html          # Página principal con formulario y tabla
│   └── edit.html           # Página de edición de items
└── static/
    ├── styles.css          # Estilos CSS
    └── formatter.js        # JavaScript para formateo de moneda
```

## Requisitos del Sistema

- **CMake** 3.16 o superior
- **Compilador C++17:** Visual Studio 2019+, MinGW (MSYS2), GCC, o Clang
- **Sin dependencias externas** - solo librerías estándar del sistema

## Compilación

### Usando CMake (recomendado)

```powershell
# Navegar al directorio del proyecto
cd pilotoDeMonetizacionCSFJ

# Generar archivos de build
cmake -B build -S .

# Compilar en modo Release
cmake --build build --config Release
```

El ejecutable se genera en:

- **Visual Studio:** `build/Release/pilotoDeMonetizacionCSFJ.exe`
- **MinGW:** `build/pilotoDeMonetizacionCSFJ.exe`

### Compilación rápida con g++ (MinGW/MSYS2)

```powershell
g++ -std=c++17 -Wall -Wextra -O2 src/main.cpp -lws2_32 -o pilotoDeMonetizacionCSFJ.exe
```

### En Linux/macOS

```bash
g++ -std=c++17 -Wall -Wextra -O2 src/main.cpp -o pilotoDeMonetizacionCSFJ
```

## Ejecución

1. Ejecutar el servidor:

   ```powershell
   ./build/Release/pilotoDeMonetizacionCSFJ.exe
   ```

2. Abrir en el navegador: <http://localhost:8080>

3. Para detener el servidor: presionar `Ctrl+C` en la terminal

## Guía de Uso

### Agregar un Item

1. Seleccionar una categoría del dropdown o elegir "Otro..." para nombre personalizado
2. Ingresar la cantidad (número entero positivo)
3. Ingresar el costo unitario (el formato de moneda se aplica automáticamente)
4. Hacer clic en "Agregar"

### Editar un Item

1. Hacer clic en el botón "Editar" de la fila correspondiente
2. Modificar los campos deseados
3. Hacer clic en "Guardar cambios"

### Exportar a CSV

1. Hacer clic en el botón "Exportar CSV"
2. Se descargará un archivo `items.csv` con todos los items y totales

---

## Documentación Técnica

### Rutas HTTP Implementadas

| Método | Ruta | Descripción |
|--------|------|-------------|
| GET | `/` | Página principal con tabla de items |
| GET | `/index.html` | Alias de la página principal |
| GET | `/edit?index=N` | Página de edición del item en posición N |
| GET | `/export` | Descarga archivo CSV |
| GET | `/static/*` | Archivos estáticos (CSS, JS) |
| POST | `/submit` | Agregar nuevo item |
| POST | `/update` | Actualizar item existente |

### Almacenamiento de Datos

- Los datos se almacenan **únicamente en memoria** durante la ejecución
- Al detener el servidor, todos los datos se pierden
- Estructura de item: `{nombre, cantidad, costoUnitario}`
- Acceso thread-safe mediante mutex

### Formato de Moneda

El formateo se realiza tanto en backend (C++) como en frontend (JavaScript):

- Separadores de miles alternados: `'` y `,`
- Siempre 2 decimales
- Ejemplo: `1'234,567.89`

---

## Guía de Modificación

### Agregar nuevas categorías al dropdown

Editar `templates/index.html`, buscar el elemento `<select id="itemDropdown">` y agregar:

```html
<option value="Nueva Categoría">Nueva Categoría</option>
```

### Cambiar el puerto del servidor

Editar `src/main.cpp`, buscar la constante:

```cpp
constexpr int PORT = 8080;
```

Cambiar `8080` por el puerto deseado.

### Agregar una nueva ruta HTTP

En `src/main.cpp`, dentro de la función `handleRequest()`:

1. Agregar la condición para la nueva ruta:

   ```cpp
   else if (method == "GET" && path == "/nueva-ruta") {
       // Tu lógica aquí
       response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h1>Nueva página</h1>";
   }
   ```

2. Recompilar el proyecto

### Modificar estilos

Editar `static/styles.css`. Los cambios se reflejan al recargar la página (no requiere recompilar).

### Agregar persistencia de datos

Para guardar datos de forma permanente, se necesitaría:

1. Agregar una librería de base de datos (SQLite recomendado) o
2. Implementar guardado/lectura a archivo JSON/CSV
3. Modificar las funciones de agregar/editar/eliminar items

---

## Limitaciones Conocidas

- **Sin persistencia:** Los datos existen solo mientras el servidor está activo
- **Sin autenticación:** Cualquier usuario en la red puede acceder
- **Single-threaded:** Procesa una solicitud a la vez (suficiente para uso individual)
- **Sin HTTPS:** Las conexiones no están cifradas

## Solución de Problemas

### El puerto 8080 está ocupado

```powershell
# Ver qué proceso usa el puerto
netstat -ano | findstr :8080

# Terminar el proceso (reemplazar PID con el número obtenido)
taskkill /PID <PID> /F
```

O cambiar el puerto en `src/main.cpp` (ver sección de modificación).

### Error de compilación: "ws2_32 not found"

Asegurarse de estar en Windows y que el compilador tiene acceso a las librerías del sistema. Con MinGW, verificar que MSYS2 esté correctamente instalado.

### Los templates no se encuentran

Ejecutar el servidor desde el directorio raíz del proyecto, donde se encuentran las carpetas `templates/` y `static/`.

### Los cambios en HTML/CSS no se reflejan

Limpiar la caché del navegador (`Ctrl+Shift+R`) o abrir en modo incógnito.

---

## Licencia

Proyecto interno de la Convocatoria San Francisco Javier.
