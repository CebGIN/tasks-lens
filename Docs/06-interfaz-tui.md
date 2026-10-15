# 06. Interfaz TUI

La interfaz se construye en `build_scene()` (`src/main.cpp`, líneas 198-341) y
utiliza el motor GuayabaTUIEngine. Este documento describe cómo se arma la
pantalla y las reglas de layout que hay que respetar.

## 6.1. Reglas de layout del motor

Estas reglas explican decisiones que, de otro modo, parecen arbitrarias. Son la
fuente del clásico problema "todo aparece centrado".

- **Alineación del contenido.** `Container`, `VBox` y `HBox` colocan el bloque de
  sus hijos según `set_content_alignment(ContentAlignment)`. El valor por
  defecto es `Center`. Un `VBox` que ocupa toda la pantalla centra la columna
  entera salvo que se pase `ContentAlignment::Begin`. De ahí que todas las
  filas de la pantalla usen `Begin`.
- **Anchors.** Un nodo se coloca dentro del rectángulo de anclaje, que por
  defecto es un punto (`TopLeft`). `AnchorPreset::FullRect` hace que el nodo
  abarque todo el rectángulo del padre.
- **`Grow`.** `set_grow_x` / `set_grow_y` con `Grow::Both` permite que un hijo se
  expanda hacia el espacio libre. El crecimiento se resuelve *contra el
  rectángulo de anclaje*: para que `grow_x` tenga efecto, el ancla derecha debe
  llegar a `1.0`. Por eso muchas filas combinan `set_anchor_right(1.0f)` con
  `set_grow_x(Grow::Both)`.
- **Eje transversal.** En `VBox` y `HBox`, las celdas de los hijos abarcan todo
  el eje transversal; `Grow::Both` en el eje principal hace que la celda del hijo
  se expanda hacia el espacio sobrante.
- **`PanelContainer`.** Ignora la alineación: estira su único hijo dentro de
  `get_stylebox().set_content_margin(...)`. Conviene usar márgenes simétricos.
- **`Label` y tamaño mínimo.** Una `Label` no informa tamaño mínimo salvo que se
  active `set_fit_content(true)`. Sin eso, un `VBox` o `HBox` mide la fila con
  altura `0`. Todas las etiquetas de texto llevan `set_fit_content(true)`.

## 6.2. Patrón de pantalla completa

La estructura general sigue este patrón:

```
PanelContainer (FullRect, borde doble)   ← "panel"
  └── VBox (FullRect, Begin)             ← "layout"
        ├── 1. fila de título
        ├── 2. fila de búsqueda
        ├── 3. resumen del sistema         (etiqueta)
        ├── 4. resumen de CPU/mem/carga    (etiqueta)
        ├── 5. fila de acciones
        ├── 6. tabla de procesos           (crece y absorbe el espacio libre)
        └── 7. línea de ayuda/estado
```

El `VBox` "layout" usa `ContentAlignment::Begin`, de modo que las filas se
apilan desde arriba. La tabla es la **única** fila que crece
(`set_grow_y(Grow::Both)`): absorbe el espacio sobrante, lo que empuja la línea
de ayuda al fondo. La tabla además activa `set_clip_children(true)` para que las
filas de más no se dibujen sobre la línea de ayuda.

## 6.3. Las filas, una por una

### 1. Fila de título (líneas 218-237)

Un `HBox` con el título a la izquierda y los botones globales a la derecha. El
título crece (`grow_x`) para empujar los botones contra el borde derecho. Los
botones son **Sort**, **Interval** y **Quit**.

### 2. Fila de búsqueda (líneas 241-263)

La etiqueta `Search:` y un `LineEdit` (el filtro por nombre o usuario), más un
botón **Clear**. El `LineEdit` es la vía del ratón hacia el filtrado: toma el
foco al hacer clic. El propio campo se encarga de capturar el foco.

### 3 y 4. Resumen del sistema (líneas 266-274)

Dos etiquetas que el `Monitor` rellena en cada refresco:

- `summary_tasks`: total de procesos, en ejecución, detenidos y zombie.
- `summary_mem`: CPU global, memoria total/usada/disponible, carga media e
  intervalo actual.

Su texto se recorta al ancho de pantalla con `fit_width` (ver
[08-monitor-y-hotkeys.md](08-monitor-y-hotkeys.md)), porque una etiqueta
`fit_content` más ancha que la pantalla agrandaría el `VBox` completo y
desplazaría los botones alineados a la derecha fuera del borde.

### 5. Fila de acciones (líneas 278-300)

A la izquierda, la etiqueta `selected` con el proceso seleccionado; a la derecha,
los botones de acción: **Kill**, **Force**, **Stop**, **Cont**, **Nice**,
**Threads**, **Tree** y **History**. La etiqueta de selección crece para que los
botones queden pegados al borde derecho.

### 6. La tabla (líneas 89-151)

Es la estructura más elaborada. Se compone de:

```
PanelContainer (FullRect, borde doble)   ← "table"
  └── HBox (FullRect, Begin, separación 1)
        ├── VBox "col_pid"    → botón de cabecera + VBox de filas
        ├── VBox "col_ppid"   → ...
        ├── ...
        └── VBox "col_command" (la columna que rellena)
```

Cada columna es un `VBox` que apila:

1. Un `Button` de cabecera, que además de mostrar el título **es el
   disparador del ordenamiento**.
2. Un `VBox` de filas, que el `TableController` llena con una etiqueta por fila
   visible.

El ancho de cada columna se fija con `set_custom_minimum_size(Vec2{width, 0})`.
La columna `COMMAND` es la única con `fills: true`: conserva su ancho mínimo
pero `grow_x` le permite absorber todo el espacio horizontal que las columnas
fijas dejan libre, para no cortar los comandos largos.

### 7. Línea de ayuda / estado (líneas 306-308)

Una etiqueta con los atajos de teclado y, cuando corresponde, el filtro activo o
un mensaje de estado.

## 6.4. Orden de ensamblado y scripts

`build_scene()` termina asociando los dos scripts:

1. `table->attach_script<tasks_lens::TableController>(columnas)` — la tabla pasa
   a tener un controlador que gobierna orden, selección y ventana de filas.
2. `root->attach_script<tasks_lens::Monitor>(source, ..., controlador)` — la
   raíz pasa a tener el "cerebro", que recibe el `ProcessSource`, todas las
   etiquetas, los botones y el puntero al controlador.

El `help_text()` que devuelve el `Monitor` se escribe en la línea de ayuda al
final del ensamblado.

## 6.5. Vistas superpuestas (overlays)

El motor no tiene un concepto de "visibilidad" de nodos, así que las vistas de
detalle (hilos, árbol, historial) y el diálogo de confirmación se implementan
como **paneles añadidos a la raíz bajo demanda** y liberados con `queue_free()`.
Un overlay se dibuja por encima (es el último hijo) y recibe la entrada primero,
por lo que cubre la tabla y sus atajos mientras está abierto. Se construyen en
el momento, no en `build_scene()`, porque existen solo mientras la vista está
abierta.

Véase también: [07-tabla-y-controlador.md](07-tabla-y-controlador.md).
