# 09. Formatos y estilos

Reúne los ayudantes de formato de texto, el aspecto común de los botones y el
funcionamiento de la marquesina.

## 9.1. Formato de celdas (`src/Scripts/format.h`)

### `pad(text, width)`

Alinea a la izquierda y **recorta o rellena** hasta `width`. Todas las celdas de
una columna tienen el mismo tamaño, lo que mantiene las columnas alineadas. Si
el texto es más largo que `width`, se corta; si es más corto, se rellena con
espacios a la derecha.

### `format_bytes(bytes)`

Tamaño de memoria residente con una cifra decimal y unidad, en base 1024:

| Rango | Formato | Ejemplo |
|---|---|---|
| ≥ 1 GiB | `%.1fG` | `1.5G` |
| ≥ 1 MiB | `%.1fM` | `12.4M` |
| ≥ 1 KiB | `%.0fK` | `512K` |
| resto | `%lluB` | `340B` |

### `format_cpu_time(ns)`

Tiempo de CPU acumulado como `M:SS`, o `H:MM:SS` cuando pasa de una hora.

### `format_percent(value)`

Un número con una cifra decimal (`%.1f`). Se usa en `%CPU`, `%MEM` y la carga
media.

### `process_state_code(state)`

Definido en `src/Data/process_info.h`: devuelve la letra canónica (`R`, `S`,
`D`, `T`, `Z`, `I`, `?`).

## 9.2. Texto por celda (`src/Scripts/table_controller.h`, líneas 37-51)

`cell_text(proc, key, width)` elige el formateador según la clave de la columna:

| Clave | Formato aplicado |
|---|---|
| `pid`, `ppid`, `thr`, `pri` | `std::to_string` + `pad` |
| `user` | `pad` |
| `state` | `process_state_code` + `pad` |
| `cpu`, `mem` | `format_percent` + `pad` |
| `res` | `format_bytes` + `pad` |
| `time` | `format_cpu_time` + `pad` |
| `command` | `pad` |

## 9.3. Colores

- **Colores por estado de fila** (`row_color`, `table_controller.h` líneas
  54-58): zombie en rojo brillante, dormido en disco (`D`) en amarillo brillante,
  el resto en blanco.
- **Cabecera de columna** (`apply_header_normal_style`, líneas 22-34): fondo azul
  (azul brillante si es la columna activa) y texto cian (verde brillante si es la
  activa). Es la única fuente de verdad del aspecto de una cabecera.
- **Fila seleccionada:** fondo `BRIGHT_BLACK` como banda, para que el color de
  texto del estado siga siendo legible por encima.

### Estados de un botón (`src/Scripts/ui_style.h`)

`apply_tool_button_style` define, en un solo lugar, el aspecto de todos los
botones de acción de la aplicación:

| Estado | Fondo | Texto |
|---|---|---|
| Normal | azul | cian |
| Hover (puntero encima) | azul brillante | amarillo brillante |
| Pressed (mantenido) | blanco brillante | negro |
| Disabled (sin objetivo) | negro | negro brillante |

El estado `Disabled` es el único cuyo color de texto no trae el valor por
defecto, así que se pasa por la variante de nombre (`"BRIGHT_BLACK"`), porque no
existe una constante `BRIGHT_BLACK`.

`add_tool_button(parent, name, label)` crea un botón con ese aspecto y lo
devuelve, para que quien lo llama pueda conectar su señal `clicked` o
activarlo/desactivarlo. Al centralizar el aspecto, la barra de herramientas, el
botón **Close** de los overlays y los botones **Yes/No** de la confirmación no
pueden divergir.

## 9.4. La marquesina (`src/Scripts/marquee.h`)

`Marquee` es una marquesina de una línea para una celda que puede ser más ancha
que su columna (el comando del proceso seleccionado).

- **Cuando el texto entra en la ventana:** se muestra entero, rellenado hasta el
  ancho de la ventana.
- **Cuando no entra:** desplaza una ventana de `width` celdas por el texto una
  celda a la vez, y vuelve al inicio pasando por un pequeño hueco, para que la
  unión se note y el comando completo se pueda leer.

Todo se mide en **puntos de código**, igual que la grilla del motor, de modo que
un glifo multibyte ocupa una celda y nunca se parte por la mitad.

Parámetros y comportamiento:

- `kStepSeconds = 0.2`: segundos por celda. Lento para leer, rápido para que
  incluso un comando largo dé la vuelta en unos segundos.
- `set(text, width)`: apunta la marquesina a un texto nuevo. Si cambia el texto o
  el ancho, reinicia en la primera celda, para que una selección nueva se lea
  desde el principio.
- `advance(delta_time)`: avanza la ventana. No hace nada si el texto entra.
- `visible()`: devuelve exactamente `width` celdas, de modo que el ancho mínimo
  de la etiqueta no cambia mientras se desplaza (la fila no se reajusta).

La marquesina solo guarda el desplazamiento y el reloj; el dueño decide a qué
celda aplicarla y escribe `visible()` en esa etiqueta. Eso mantiene el tiempo
fuera del bucle por celda y hace que el desplazamiento sea una propiedad de la
selección, no de una etiqueta concreta.
