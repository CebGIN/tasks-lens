# Documentación técnica de tasks-lens

`tasks-lens` es un monitor de procesos a pantalla completa para terminal, escrito
en C++17 sobre el motor de interfaz **GuayabaTUIEngine**. Obtiene todos sus datos
directamente de la interfaz que expone el sistema operativo (en Linux, los
archivos de `/proc`), calcula los porcentajes de CPU en el propio código y
permite actuar sobre los procesos.

Esta carpeta reúne la documentación técnica separada por secciones. Cada
documento es autocontenido, pero las referencias cruzadas permiten profundizar.

## Índice

| Documento | Contenido |
|---|---|
| [01-arquitectura.md](01-arquitectura.md) | Capas del programa, grafo de escena y recorrido de un dato |
| [02-capa-de-datos.md](02-capa-de-datos.md) | Tipos de datos, la interfaz `ProcessSource` y su fábrica |
| [03-backend-linux.md](03-backend-linux.md) | Lectura y análisis de `/proc` en detalle |
| [04-backend-windows.md](04-backend-windows.md) | Backend de Windows (Tool Help / PSAPI) y sus diferencias |
| [05-cpu-y-memoria.md](05-cpu-y-memoria.md) | Fórmulas de %CPU, %MEM y el `CpuSampler` |
| [06-interfaz-tui.md](06-interfaz-tui.md) | Grafo de escena, reglas de layout y estructura de la pantalla |
| [07-tabla-y-controlador.md](07-tabla-y-controlador.md) | El `TableController`: columnas, orden y selección |
| [08-monitor-y-hotkeys.md](08-monitor-y-hotkeys.md) | El `Monitor`: bucle, atajos, acciones y vistas |
| [09-formatos-y-estilos.md](09-formatos-y-estilos.md) | Formato de celdas, colores y marquesina |
| [10-compilacion-y-ejecucion.md](10-compilacion-y-ejecucion.md) | Compilación, submódulo y modo de verificación |
| [FAQ.md](FAQ.md) | Preguntas frecuentes con respuestas directas |
| [UML_tasks-lens.drawio](UML_tasks-lens.drawio) | Diagrama UML de clases (abrir con [draw.io](https://app.diagrams.net) o la extensión Draw.io de VS Code) |

## Diagrama UML

`UML_tasks-lens.drawio` contiene dos páginas:

- **Diagrama de clases**: las clases de las tres capas (tipos de datos,
  `ProcessSource` y sus backends, `CpuSampler`, `Monitor`, `TableController`,
  `Marquee`, y las utilidades), con sus atributos, métodos y relaciones
  (herencia, composición, agregación, asociación y dependencia).
- **Leyenda**: el significado de los símbolos `+`, `-`, `#` y de cada tipo de
  flecha.

Los nombres de las clases y sus miembros coinciden con el código; los detalles de
cada una están en los documentos de la tabla de arriba.

## Convenciones de estos documentos

- Los identificadores de código se citan tal cual aparecen en el código fuente
  (en inglés).
- Las referencias del tipo `src/main.cpp:198` apuntan al archivo y a la línea de
  la revisión documentada. Si el código cambia, el número puede desplazarse; el
  nombre de la función o de la estructura sigue siendo el ancla confiable.
- La prosa está en español; el código y los nombres técnicos, en inglés.

## Glosario

| Término | Significado |
|---|---|
| **PID** | *Process ID*: identificador de un proceso. |
| **PPID** | *Parent PID*: identificador del proceso padre. |
| **TID** | *Thread ID*: identificador de un hilo dentro de un proceso. |
| **RSS** | *Resident Set Size*: memoria física residente de un proceso. |
| **nice** | Valor de prioridad de planificación en Linux, de `-20` (más prioridad) a `19` (menos prioridad). |
| **jiffy** | Unidad de tiempo del núcleo; su tamaño depende de `_SC_CLK_TCK` (habitualmente 100 Hz). |
| **Snapshot** | Instantánea de los procesos y del sistema en un momento dado. |
| **Refresh** | Relectura del sistema operativo que produce un nuevo snapshot. |
| **Script** | Objeto de lógica asociado a un nodo del motor (`guayaba::Script<T>`). |
| **Nodo** | Elemento del árbol de interfaz (`Node`, `Container`, `Label`, `Button`, ...). |
| **Overlay** | Panel a pantalla completa que se superpone por encima de la vista principal. |
