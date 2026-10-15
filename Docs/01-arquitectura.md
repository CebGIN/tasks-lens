# 01. Arquitectura general

## 1.1. Idea general

El programa tiene tres capas bien separadas:

```
┌──────────────────────────────────────────────────────────────┐
│  Interfaz (src/main.cpp + src/Scripts/)                      │
│  Grafo de escena, tabla, atajos, acción sobre procesos       │
└──────────────────────────────┬───────────────────────────────┘
                               │  usa los tipos de datos
┌──────────────────────────────┴───────────────────────────────┐
│  Datos (src/Data/)                                           │
│  Tipos neutrales + interfaz ProcessSource + CpuSampler       │
└──────────────────────────────┬───────────────────────────────┘
                               │  una implementación por sistema
┌──────────────────────────────┴───────────────────────────────┐
│  Backends (src/Data/platform/)                               │
│  Linux (/proc)  ·  Windows (Tool Help / PSAPI)               │
└──────────────────────────────────────────────────────────────┘
```

- **Datos**: define las estructuras (`ProcessInfo`, `ThreadInfo`, `SystemInfo`)
  y una interfaz abstracta, `ProcessSource`, que cada sistema operativo
  implementa. Nada de esta capa conoce la interfaz gráfica ni el motor.
- **Backends**: traducen las estructuras nativas del sistema operativo
  (líneas de `/proc`, estructuras Win32) a los tipos neutrales. La única
  responsabilidad de un backend es *leer* y *actuar*; no calcula porcentajes.
- **Interfaz**: construye el grafo de escena y lo conduce con dos scripts,
  `TableController` (presentación de la tabla) y `Monitor` (datos y atajos).

La regla que sostiene la separación es la unidad **nanosegundos**: cualquier
backend normaliza el tiempo de CPU a nanosegundos, de modo que un único
`CpuSampler` hace las matemáticas para todos los sistemas.

## 1.2. Archivos y responsabilidad

| Archivo | Responsabilidad |
|---|---|
| `src/main.cpp` | Construye el grafo de escena, arma la tabla, cablea los scripts y arranca el bucle. |
| `src/Data/process_info.h` | Estructuras neutrales y `enum class ProcessState`. |
| `src/Data/process_source.h` | Interfaz `ProcessSource` y declaración de la fábrica. |
| `src/Data/process_source_factory.cpp` | Elige el backend según la plataforma. |
| `src/Data/cpu_sampler.h/.cpp` | Convierte contadores acumulados en porcentajes entre refrescos. |
| `src/Data/platform/linux/linux_process_source.cpp` | Backend de Linux (lee `/proc`). |
| `src/Data/platform/windows/windows_process_source.cpp` | Backend de Windows (Tool Help / PSAPI). |
| `src/Scripts/table_controller.h` | Orden, selección y ventana visible de la tabla. |
| `src/Scripts/monitor.h` | El "cerebro": datos, atajos, acciones, vistas de detalle. |
| `src/Scripts/format.h` | Formato de celdas (bytes, tiempo, porcentaje, relleno). |
| `src/Scripts/ui_style.h` | Un único aspecto para todos los botones de acción. |
| `src/Scripts/marquee.h` | Desplazamiento horizontal del comando seleccionado. |

## 1.3. Los dos scripts

Un `Script` es un objeto de lógica que el motor asocia a un nodo y al que llama
en momentos conocidos (`on_attached`, `on_process`, `on_input`). El programa usa
dos:

- **`TableController`** — asociado al panel de la tabla. Sabe qué columnas hay,
  cómo se ordenan, qué fila está seleccionada y cuántas filas caben. No lee datos
  del sistema operativo: recibe la lista ya filtrada y ordenada y la pinta.
- **`Monitor`** — asociado a la raíz. Es dueño del `ProcessSource`, del
  `CpuSampler` y del último snapshot. Cada refresco relee el sistema, y cada
  fotograma ordena el snapshot y se lo entrega al `TableController`. También
  gestiona los atajos, las acciones y las vistas de detalle.

La división importa: `TableController` responde a *cómo se ve* la tabla (orden,
desplazamiento, selección), mientras que `Monitor` responde a *qué datos hay* y
*a qué acción corresponde cada tecla*.

## 1.4. Recorrido de un dato

Cómo viaja un valor desde el núcleo hasta una celda de la pantalla:

```
 1. Núcleo (Linux: /proc/<pid>/stat, /status, /cmdline)
        │
 2. LinuxProcessSource::read_processes()   ← lo analiza y lo traduce
        │  produce  std::vector<ProcessInfo>   (cpu_time_ns acumulado)
        │
 3. CpuSampler::apply()                    ← diferencia contra el refresco previo
        │  rellena  cpu_percent y mem_percent
        │
 4. Monitor::refresh()                     ← guarda el snapshot en processes_
        │
 5. Monitor::rebuild_visible()             ← aplica el filtro y el orden
        │  produce  visible_
        │
 6. TableController::render()              ← escribe la ventana visible en etiquetas
        │  (usa cell_text y format.h)
        │
 7. guayaba::Label                         ← el motor lo dibuja en el terminal
```

Un detalle clave: los pasos 2 y 3 están separados por diseño. El backend solo
informa el **tiempo acumulado** de CPU; el porcentaje necesita el refresco
anterior, y esa memoria vive en el `CpuSampler`, no en el backend. Así ningún
backend tiene que repetir esa cuenta.

## 1.5. Plataformas soportadas

La fábrica `create_system_process_source()` (en
`src/Data/process_source_factory.cpp`) elige el backend en tiempo de
compilación mediante `#if defined(_WIN32)` / `#elif defined(__linux__)`. Si la
plataforma no es ninguna de las dos, la compilación falla con un `#error`.

Ambos backends implementan la misma interfaz, así que todo lo que está por
encima de `ProcessSource` es independiente del sistema operativo. En la práctica
el backend de Windows está compilado y estructurado, pero sus acciones
(`terminate`, `suspend`, `resume`, `set_priority`) están sin implementar; ver
[04-backend-windows.md](04-backend-windows.md).

Véase también: [02-capa-de-datos.md](02-capa-de-datos.md) para la interfaz, y
[06-interfaz-tui.md](06-interfaz-tui.md) para la construcción de la pantalla.
