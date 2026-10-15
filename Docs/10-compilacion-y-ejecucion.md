# 10. Compilación y ejecución

## 10.1. Requisitos

- Un compilador de C++17.
- CMake 3.14 o superior.
- El motor **GuayabaTUIEngine**, que vive como submódulo de git en
  `external/GuayabaTUIEngine`.
- En Linux, además, los hilos de la biblioteca estándar (se enlaza
  `Threads::Threads`). En Windows, `psapi`.

## 10.2. Estructura de la compilación

`CMakeLists.txt` (raíz del repositorio):

- Declara el proyecto `tasks-lens` (C++17) y agrega el motor con
  `add_subdirectory(external/GuayabaTUIEngine)`.
- Recolecta las fuentes con `file(GLOB_RECURSE ... src/*.cpp)` y luego **quita**
  el archivo del backend de la otra plataforma:

  ```cmake
  if(WIN32)
      list(REMOVE_ITEM TASKS_LENS_SOURCES
          ".../src/Data/platform/linux/linux_process_source.cpp")
  else()
      list(REMOVE_ITEM TASKS_LENS_SOURCES
          ".../src/Data/platform/windows/windows_process_source.cpp")
  endif()
  ```

  Cada `.cpp` de backend está además protegido con `#if defined(__linux__)` /
  `#ifdef _WIN32`, así que la remoción es higiene: evita compilar un archivo que
  de todos modos se vaciaría.
- Enlaza `GuayabaTUIEngine::GuayabaTUIEngine` y `Threads::Threads`.
- Añade `src` a las rutas de inclusión, de modo que los `#include` del proyecto
  se escriben como `"Data/process_info.h"`, `"Scripts/monitor.h"`, etc.

## 10.3. Compilar

El árbol `build/` ya está configurado con el generador Makefile. Desde la raíz:

```bash
cmake --build build
```

Para un árbol nuevo:

```bash
cmake -S . -B build
cmake --build build
```

El objetivo es el ejecutable `tasks-lens`.

**Orden que importa:** hay que reconstruir el consumidor después de cada cambio
en el motor o en el código del proyecto. La interfaz necesita un terminal real
(modo crudo); para verificar el layout sin abrir la interfaz está el binario de
pruebas del motor (ver 10.5).

## 10.4. Ejecutar

```bash
./build/tasks-lens
```

Es una aplicación a pantalla completa: toma el terminal completo, usa su altura y
ancho reales y, al salir, restaura el terminal (eco, cursor y prompt). Para salir,
la tecla `q` o el botón **Quit**.

## 10.5. Verificación sin terminal interactivo

### Modo `--dump`

El ejecutable acepta un argumento `--dump` que ejecuta una ruta de verificación
sin tomar el terminal:

```bash
./build/tasks-lens --dump
```

`run_dump()` (`src/dump.cpp`, líneas 15-52; declarado en `src/dump.h`) toma
**dos** instantáneas separadas por un segundo (para que los deltas de CPU no sean
cero), las pasa por el `CpuSampler` y muestra por `stdout` un extracto tipo
`top`: el resumen del
sistema y los diez procesos con más CPU. No hay toma de pantalla, así que se
puede usar en una tubería o redirigir a un archivo. Es la forma más rápida de
comprobar que la capa de datos funciona.

### Pruebas de layout del motor

El motor incluye pruebas de layout en modo headless, apagadas por defecto. Para
activarlas:

```bash
cmake -B external/GuayabaTUIEngine/build-tests \
      -DGUAYABA_BUILD_TESTS=ON external/GuayabaTUIEngine
cmake --build external/GuayabaTUIEngine/build-tests
external/GuayabaTUIEngine/build-tests/layout_alignment
```

El binario `layout_alignment` verifica la alineación de los contenedores sin
necesidad de un terminal real. Como el proyecto no tiene pruebas propias, esta es
la verificación de layout disponible.

## 10.6. El submódulo

`external/GuayabaTUIEngine` es un submódulo de git de otro repositorio, fijado
por una etiqueta (*tag*). Los cambios en el motor se confirman, etiquetan y
publican **en el repositorio del motor**, no en este. Después de que exista una
etiqueta nueva:

```bash
git -C external/GuayabaTUIEngine checkout v1.0.x-alpha
git add external/GuayabaTUIEngine
```

El pin solo se mueve con ese `git add` explícito; hasta entonces, `git status`
muestra `M external/GuayabaTUIEngine`.

No conviene editar las fuentes del motor desde el repositorio principal sin
coordinarlo: el pin del proyecto quedaría divergente respecto del repositorio del
motor.

## 10.7. Estructura de carpetas del proyecto

```
tasks-lens/
├── CMakeLists.txt
├── Docs/                          ← esta documentación
├── src/
│   ├── main.cpp                   ← grafo de escena y arranque
│   ├── dump.cpp / dump.h          ← ruta de verificación `--dump`
│   ├── Data/                      ← capa de datos neutral
│   │   ├── process_info.h
│   │   ├── process_source.h
│   │   ├── process_source_factory.cpp
│   │   ├── cpu_sampler.h / .cpp
│   │   ├── history_store.h
│   │   ├── process_tree.h
│   │   └── platform/
│   │       ├── linux/linux_process_source.h / .cpp
│   │       └── windows/windows_process_source.h / .cpp
│   └── Scripts/                   ← lógica de interfaz (solo cabeceras)
│       ├── monitor.h
│       ├── table_controller.h
│       ├── table_layout.h
│       ├── process_view.h
│       ├── process_sort.h
│       ├── text_util.h
│       ├── viewport.h
│       ├── format.h
│       ├── priority_input.h
│       ├── confirm_dialog.h
│       ├── detail_overlay.h
│       ├── ui_style.h
│       └── marquee.h
└── external/
    └── GuayabaTUIEngine/          ← submódulo: motor de TUI
```
