# 08. El `Monitor`: bucle, atajos y acciones

El `Monitor` (`src/Scripts/monitor.h`) es un `Script<Node>` asociado a la raíz.
Es dueño de los datos (el `ProcessSource`, el `CpuSampler` y el último
snapshot) y de los atajos. Cada fotograma ordena el snapshot según la tabla y se
lo entrega al `TableController`; cada intervalo de refresco vuelve a leer el
sistema operativo.

Está en la raíz, así que su `on_input` se ejecuta **después** de que todos los
controles del árbol hayan tenido su turno: lo que un control consume (por
ejemplo, teclas escritas en el campo de búsqueda) nunca llega a estos atajos
globales.

## 8.1. El bucle de fotograma: `on_process` (líneas 110-159)

En cada fotograma:

1. Acumula `elapsed_ += delta_time`.
2. Si es el primer fotograma o `elapsed_ >= interval_`, llama a `refresh()`,
   reinicia el contador y marca `dirty_ = true`.
3. Si cambió el orden (clave o dirección) respecto del fotograma anterior, marca
   `dirty_ = true`.
4. Si `dirty_`, reconstruye `visible_` con `rebuild_visible()`.
5. Sincroniza el foco de la barra de búsqueda (`sync_search_focus`).
6. Entrega `visible_` al `TableController::render`.
7. Actualiza la etiqueta de selección y el estado de la barra de herramientas.
8. Si hay una vista abierta y el contenido está sucio, la actualiza.
9. Si hay un mensaje de estado pendiente, va descontando su temporizador.

La reconstrucción de `visible_` solo ocurre cuando algo que la cambia cambió (un
snapshot nuevo, una edición del filtro o un cambio de orden); de otro modo se
estarían copiando y reordenando todos los procesos 30 veces por segundo.

## 8.2. `refresh()` (líneas 299-324)

Relee el sistema, aplica el `CpuSampler` y calcula el %CPU global:

```cpp
SystemInfo system = source_->read_system();
std::vector<ProcessInfo> processes = source_->read_processes();
sampler_.apply(system, processes);
// ... system_cpu_percent_ = (Δtotal − Δidle) / Δtotal ...
system_ = system;
processes_ = std::move(processes);
record_history();
content_dirty_ = true;
update_summary();
```

## 8.3. `rebuild_visible()` (líneas 371-395)

Produce la lista que ve la tabla: aplica el filtro y ordena según la columna
activa. El snapshot original (`processes_`) queda intacto, de modo que una acción
todavía puede alcanzar a un proceso que el filtro está ocultando.

El comparador usa la clave y la dirección activas, y ante empates (por ejemplo,
todos los procesos de `root` bajo la columna `USER`) **desempata por PID
ascendente**. Como el PID es único, el comparador es un orden total y las filas
equivalentes mantienen su posición entre refrescos en lugar de reordenarse según
lo que devolvió `readdir`.

## 8.4. Filtro (elemento 13 del enunciado)

- `matches_filter()` (líneas 408-411): prueba de subcadena **sin distinguir
  mayúsculas** (`contains_ci`) tanto en el comando como en el usuario, así que
  tanto `root` como `firefox` funcionan.
- El filtrado es en vivo mientras se escribe (`on_prompt_changed`, líneas
  479-485).

## 8.5. Atajos de teclado

Los atajos viven en `on_input` (líneas 161-294). El orden de precedencia es
importante: primero el ratón, luego la confirmación pendiente, luego el modo de
entrada, luego la vista abierta y, por último, los atajos de la tabla.

### Teclado

| Tecla | Acción |
|---|---|
| `q` | Salir. |
| `s` | Cambiar el orden (ciclo de columna; repetir invierte la dirección). |
| `i` | Cambiar el intervalo de refresco (1, 2, 5, 10 s). |
| `k` | Terminar el proceso seleccionado (`SIGTERM`). |
| `K` (Shift) | Terminar a la fuerza (`SIGKILL`), con confirmación previa. |
| `z` | Suspender (`SIGSTOP`). |
| `c` | Reanudar (`SIGCONT`). |
| `f` o `/` | Entrar al filtro por nombre. |
| `p` | Cambiar la prioridad (nice) del proceso seleccionado. |
| `t` | Ver los hilos del proceso seleccionado. |
| `r` | Ver el árbol de procesos. |
| `g` | Ver el historial de CPU del proceso seleccionado. |
| `↑` / `↓` | Mover la selección una fila. |
| `PgUp` / `PgDn` | Mover la selección una página. |
| `Home` / `End` | Ir a la primera / última fila. |

### Ratón

| Entrada | Acción |
|---|---|
| Rueda | Mueve la selección (la rueda mueve el cursor, que es el ancla del desplazamiento, no la ventana: así nunca se separan). |
| Clic izquierdo en una fila | Selecciona esa fila y pone el foco fuera de la búsqueda. |
| Clic en una cabecera | Ordena por esa columna (lo consume el `Button` antes de llegar a la raíz). |

### Diálogos modales

- **Confirmación de force kill:** solo responde `y` (sí), `n` o `Escape` (no).
  Cualquier otra tecla se traga, para que ninguna acción pueda dispararse
  mientras el diálogo está abierto.
- **Modo de entrada (filtro / prioridad):** el campo tiene el foco y consume las
  teclas y `Enter`; lo único que llega a la raíz es `Escape` (cancelar).
- **Vista de detalle:** su `ListView` consume las teclas de navegación y
  `Escape` cierra la vista. Todo lo demás se traga.

## 8.6. Modos de entrada: `InputMode` (líneas 415-543)

- **`Filter`:** entra con `f` o `/`, o al hacer clic en el campo. Mientras se
  escribe, filtra en vivo. `Escape` cancela y limpia.
- **`Priority`:** entra con `p`. El campo se deja **vacío** a propósito (no se
  rellena con el nice actual: editarlo en el cursor haría que escribir "5" sobre
  "0" leyera "50"). El valor actual se muestra en la línea de estado. `Enter`
  aplica lo escrito.

`apply_priority()` (líneas 523-543) valida el número, lo **recorta al rango
`-20..19`** (el rango de nice) y llama a `source_->set_priority`. Si tiene éxito,
adelanta el próximo refresco (`elapsed_ = interval_`) para que la columna `PRI`
refleje el cambio de inmediato.

Al salir del modo, el texto del filtro se restaura, de modo que una edición de
prioridad abandonada no "se come" el filtro.

## 8.7. Acciones sobre procesos

Todas apuntan al proceso bajo el cursor, que se obtiene con un único accesor,
`selected_process()` (líneas 642-647).

- **`terminate_selected(force)`** (líneas 649-666): llama al backend y, si tiene
  éxito, adelanta el refresco para que la fila desaparezca en el mismo fotograma.
- **`request_force_kill()`** (líneas 668-675): abre la confirmación.
- **`open_confirm` / `close_confirm` / `on_confirm_yes` / `on_confirm_no`**
  (líneas 681-767): el diálogo modal de Yes/No. `confirm_pid_` cumple doble
  función: es el PID a confirmar y, siendo distinto de cero, significa "hay un
  diálogo abierto".
- **`set_suspended(suspend)`** (líneas 769-785): `SIGSTOP` o `SIGCONT`.
- **`report_failure(pid)`** (líneas 790-792): el mensaje del elemento 15 del
  enunciado. Como un `bool` no distingue entre "ya no existe" y "sin permiso", el
  texto cubre ambos: `PID <n>: failed (gone or no permission)`.

Tras cada acción con éxito se llama a `elapsed_ = interval_` para forzar un
refresco inmediato.

## 8.8. Barra de herramientas y estado de botones (líneas 824-862)

`update_toolbar()` corre cada fotograma: desactiva los botones de acción cuando
no hay selección y mantiene actualizadas las etiquetas de **Sort** (columna y
flecha) e **Interval** (segundos). Los `set_text` están protegidos para no
reescribir los botones 30 veces por segundo. Los botones usan el aspecto común de
`ui_style.h` (ver [09-formatos-y-estilos.md](09-formatos-y-estilos.md)).

## 8.9. Vistas de detalle: `ViewKind` (líneas 872-1062)

Hay tres vistas, `Threads`, `Tree` y `History`, más `None`. Cada una se abre con
un panel a pantalla completa añadido a la raíz (un overlay) y se libera con
`queue_free()` al cerrar.

- **Threads** (líneas 970-982): llama a `source_->read_threads(pid)` y arma una
  línea por hilo con `thread_line()` (tid, estado, tiempo de CPU, nombre).
- **Tree** (líneas 984-992): `tree_lines()` (líneas 1018-1039) construye el
  árbol padre → hijo de toda la máquina. Las raíces son los procesos cuyo padre
  no está en el snapshot; todo lo que quede (un ciclo, o un padre que desapareció
  a mitad de la lectura) se muestra en profundidad cero para no perder nada. La
  recursión `append_subtree` tiene un límite de profundidad (64) y un conjunto
  `visited` que evita bucles.
- **History** (líneas 994-1007): busca la serie del PID en `history_` y se la
  pasa a un `Graph` con autoescalado.

La actualización ocurre cuando `content_dirty_` está activo, de modo que una
vista abierta se refresca con cada snapshot nuevo (ver
[05-cpu-y-memoria.md](05-cpu-y-memoria.md) para el historial).

## 8.10. `fit_width` y recorte de texto (líneas 600-634)

Dos ayudantes evitan que las líneas largas ensanchen el layout:

- **`truncate(text, max)`**: corta por **bytes** y agrega puntos suspensivos.
  Es a propósito por bytes: acota el ancho mínimo de la etiqueta, que es lo que
  el layout necesita.
- **`clip_cells(text, max_cells)`**: recorta por **puntos de código** (con
  `decode_utf8`), para no partir un carácter multibyte por la mitad.
- **`fit_width(text)`**: recorta cualquier línea libre al ancho de pantalla menos
  el margen, y se usa en el resumen y en la línea de estado.

## 8.11. Mensajes y línea de ayuda

`set_message(text)` (líneas 1068-1071) escribe un mensaje y arma un temporizador
de 2 segundos; al expirar, la línea vuelve al texto de ayuda (`help_line`,
líneas 548-551), que incluye el filtro activo seguido de la lista de atajos. Si
hay una confirmación pendiente, el temporizador no corre, para no borrar el
diálogo de debajo del usuario.

Véase también: [07-tabla-y-controlador.md](07-tabla-y-controlador.md).
