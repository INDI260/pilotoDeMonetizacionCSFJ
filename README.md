# Servidor de formulario de items en C++

Aplicación web mínima escrita en C++ que levanta un servidor HTTP simple. Permite ingresar items con su costo mediante un formulario y ver el listado acumulado en memoria mientras el proceso se mantiene en ejecución.

## Requisitos

- CMake 3.16 o superior
- Compilador C++17 (Visual Studio 2019+, MSYS2 MinGW, clang, etc.)

## Compilación con CMake

```powershell
cd C:\Users\aleca\Downloads\pilotoMonetizacionCSFJ
cmake -B build -S .
cmake --build build --config Release
```

El ejecutable quedará en `build/Release/item_form_server.exe` (en modo Release) o `build/item_form_server.exe` dependiendo del generador.

## Ejecución

```powershell
cd C:\Users\aleca\Downloads\pilotoMonetizacionCSFJ\build
./Release/item_form_server.exe
```

Si usas MinGW, el binario suele quedar directamente en `build/item_form_server.exe`:

```powershell
cd C:\Users\aleca\Downloads\pilotoMonetizacionCSFJ\build
./item_form_server.exe
```

Luego abre `http://localhost:8080` en tu navegador para usar el formulario.

## Compilación rápida con g++ (MSYS2/MinGW)

```powershell
g++ -std=c++17 -Wall -Wextra -O2 src/main.cpp -lws2_32 -o item_form_server.exe
```

## Notas

- El listado de items se guarda únicamente en memoria.
- Para finalizar el servidor presiona `Ctrl+C` en la terminal.
