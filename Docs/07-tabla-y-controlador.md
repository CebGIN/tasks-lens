# 07. La tabla y el `TableController`

El `TableController` (`src/Scripts/table_controller.h`) es un
`Script<PanelContainer>` asociado al panel de la tabla. Es dueño de la
**presentación**: las columnas, el orden, la selección y la ventana de filas
visibles. No lee datos del sistema operativo; recibe la lista ya filtrada y
ordenada y la dibuja.

## 7.1. Columnas

El `main.cpp` define la disposición de columnas con una `struct ColumnSpec` local
(`key`, `title`, `width`, `fills`; líneas 20-25), volcada en la tabla `kColumns`:

| Columna | Título | Ancho | ¿Rellena? |
|---|---|---|---|
| `pid` | PID | 5 | no |
| `ppid` | PPID | 6 | no |
| `user` | USER | 10 | no |
| `state` | STATE | 6 | no |
| `cpu` | %CPU | 6 | no |
| `mem` | %MEM | 6 | no |
| `res` | RES | 8 | no |
| `thr` | THR | 5 | no |
| `pri` | PRI | 5 | no |
| `time` | TIME+ | 9 | no |
| `command` | COMMAND | 16 | **sí** |

El `TableController` recibe, por cada columna, su propio `ColumnSpec` (público,
`table_controller.h`, líneas 69-76), con los campos `key`, `width`, `min_width`,
`fills`, el `VBox` de filas (`rows`) y el `Button` de cabecera (`header`); el
`main.cpp` los arma con campos con nombre. En su constructor detecta cuál es la
columna flexible (`flex_index_`) y cuál es la de comando (`marquee_index_`).

## 7.2. Ordenamiento

Estado interno: `sort_key_` (columna activa) y `ascending_` (dirección). Por
defecto `sort_key_ = "pid"` y `ascending_ = true`.

La elección del orden inicial por PID es deliberada: ordenar por %CPU (el
"top" obvio) reordena la tabla varias veces por segundo porque los deltas
oscilan, y eso se percibe como parpadeo. El PID es un orden estable.

- **`cycle_sort()`** (`table_controller.h`, líneas 92-109): pasa a la siguiente
  columna, en orden descendente; si se vuelve a elegir la columna activa,
  invierte la dirección.
- **Clic en la cabecera** (`on_header_clicked`, líneas 234-249): activa esa
  columna o invierte su dirección. El `Button` de la cabecera es el disparador.
- **`refresh_headers()`** (líneas 251-255): repinta las cabeceras resaltando la
  columna activa.

La comparación real la aporta `process_less()` (`src/Scripts/process_sort.h`,
líneas 12-26), que es el "menor que" de cada clave. El `Monitor` lo usa para
ordenar `visible_`.

## 7.3. Selección

La selección no se guarda por **índice**, sino por **PID**
(`selected_pid_`). Esto es clave: cuando la tabla se reordena (lo que ocurre
justamente al ordenar por %CPU), un índice apuntaría a otro proceso; un PID no.
`selected_index_` se recalcula cada fotograma a partir del PID.

Métodos de selección:

- **`select_by(delta, procs)`** (líneas 122-128): mueve el cursor `delta` filas,
  con recorte en los extremos.
- **`select_to(index, procs)`** (líneas 131-135): selección absoluta, recortada.
- **`select_at(point, procs)`** (líneas 139-149): selecciona la fila bajo un
  clic, probando contra el rectángulo del `VBox` de la primera columna (todas las
  columnas comparten las mismas filas).

En `render()`, si el PID seleccionado ya no existe (el proceso terminó), la
selección cae a la primera fila, de modo que siempre hay un objetivo para una
acción.

## 7.4. Ventana de filas (virtualización)

Crear un nodo por proceso no escala: una máquina puede tener cientos. El
`TableController` mantiene, por columna, un **pool** de etiquetas del tamaño de
la ventana visible, y en `render()` escribe el segmento
`[offset_, offset_ + capacity_)` en ellas.

- `capacity_` es cuántas filas caben.
- `offset_` es la primera fila visible.
- La fila seleccionada **es el ancla del desplazamiento**: si queda fuera de la
  ventana, el offset se ajusta para volver a mostrarla. Así el cursor nunca se
  pierde de vista.

### `ensure_capacity()` (`table_controller.h`, líneas 260-324)

Calcula `capacity_` y ajusta el pool. Mide el **terminal**, no los rectángulos ya
resueltos: si usara los rectángulos del layout, la capacidad dependería de la
capacidad anterior (el panel se dimensiona según su contenido) y la página podría
crecer pero nunca encoger. El tamaño se lee con `terminal_rows()` /
`terminal_cols()` (`src/Scripts/viewport.h`), el único valor de la cadena que el
pool no infla.

Con esos datos y el "cromo" (los bordes y todas las filas de layout por encima y
por debajo de la tabla, aislado restando el tamaño mínimo del padre menos el de la
tabla), `ensure_capacity()` delega en `compute_table_layout()`
(`src/Scripts/table_layout.h`, líneas 21-42), que devuelve cuántas filas caben
(`capacity`) y el ancho de la columna flexible (`flex_width`), o indica que no hay
columna que rellenar (`has_flex`). Ese ancho parte del de pantalla, resta los
bordes dobles (el del panel exterior y el de la tabla), los espacios entre columnas
y los anchos fijos, y aplica el mínimo de la columna. En un terminal angosto, la
tabla se recorta en lugar de empujar las columnas finales fuera del borde.

### Diferencia de texto

`render()` compara cada texto nuevo con `last_text_` antes de llamar a
`set_text`, para no reescribir las etiquetas que no cambiaron. El color de fondo
de la fila seleccionada (`BRIGHT_BLACK`) se aplica como banda, de modo que el
color de texto por estado se mantiene legible.

## 7.5. Colores por estado

`row_color()` (`src/Scripts/process_view.h`, líneas 34-38) decide el color del
texto de una fila:

- **Zombie** (`Z` / `X`) → rojo brillante.
- **Dormido en disco** (`D`, esperando E/S) → amarillo brillante.
- **Resto** → blanco.

Estos son los dos estados que el enunciado pide resaltar.

## 7.6. La marquesina del comando

El comando del proceso seleccionado podría ser más ancho que su columna. En vez
de cortarlo, se muestra como una **marquesina** (`src/Scripts/marquee.h`) que
desplaza una ventana de ancho fijo por el texto completo y vuelve al inicio,
dejando un pequeño hueco en la unión.

La marquesina se avanza **una vez por fotograma** por el `TableController`, no
por celda: así el reloj es propiedad de la selección (un solo estado) y no de
una etiqueta concreta. Cuando el texto entra en la ventana, se muestra entero y
sin desplazamiento.

## 7.7. Anchos tras un cambio de tamaño

Todo el cálculo de capacidad y de ancho flexible se rehace cada fotograma, pero
solo actúa si el resultado cambió (`if (capacity == capacity_ && ...) return;`).
Esto hace que la tabla se adapte sola a un cambio de tamaño del terminal sin
reconstruir el grafo.

Véase también: [06-interfaz-tui.md](06-interfaz-tui.md) para la construcción de
la tabla y [08-monitor-y-hotkeys.md](08-monitor-y-hotkeys.md) para quien la
dirige.
