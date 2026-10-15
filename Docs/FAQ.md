# Preguntas frecuentes (FAQ)

Respuestas directas a las dudas más habituales sobre cómo funciona la
aplicación. Cada respuesta enlaza al documento donde se desarrolla el tema.

---

### ¿Qué es tasks-lens?

Un monitor de procesos a pantalla completa para terminal, escrito en C++17 sobre
el motor de TUI GuayabaTUIEngine. Muestra los procesos del sistema, su consumo de
CPU y memoria, un resumen de la máquina y permite actuar sobre los procesos.
Todos los datos se obtienen directamente de la interfaz del sistema operativo y
los porcentajes se calculan en el propio código.

---

### ¿Qué archivos de `/proc` lee, en Linux?

- `/proc/<pid>/stat` — PID, PPID, estado, tiempo de CPU, nice, hilos y RSS.
- `/proc/<pid>/status` — el UID (para el nombre de usuario).
- `/proc/<pid>/cmdline` — la línea de comandos.
- `/proc/<pid>/task/<tid>/stat` — el estado y el tiempo de cada hilo.
- `/proc/stat` — el tiempo total y el ocioso del sistema.
- `/proc/loadavg` — la carga media.
- `/proc/meminfo` — la memoria total, disponible, libre, buffers y caché.

Detalle en [03-backend-linux.md](03-backend-linux.md).

---

### ¿Qué contiene cada archivo y qué campos se usan?

- **`/proc/<pid>/stat`** es una línea de campos numerados desde 1. Se usan el 3
  (estado), 4 (PPID), 14 y 15 (`utime` y `stime`), 19 (nice), 20 (número de
  hilos) y 24 (RSS, **en páginas**). La tabla campo → índice está en
  [03-backend-linux.md](03-backend-linux.md#33-parse_stat-el-análisis-de-procpidstat-líneas-72-108).
- **`/proc/<pid>/status`** es un bloque de `Clave: valor` con tabulaciones; se
  busca la línea `Uid:` y se toma el primer número (el UID real).
- **`/proc/<pid>/cmdline`** es el vector `argv` en crudo, separado por bytes
  nulos, no una línea de texto.
- **`/proc/stat`** empieza con la línea `cpu`, con tiempos acumulados en jiffies:
  `user nice system idle iowait irq softirq steal`.
- **`/proc/meminfo`** lista valores en **kB**.
- **`/proc/loadavg`** son tres números decimales.

---

### ¿Por qué el análisis de `stat` se hace después del último `)`?

Porque el campo `comm` (el campo 2), que va entre paréntesis, **puede contener
espacios y paréntesis**. Un nombre como `mi (raro) app` rompería un *split*
ingenuo por espacios y desplazaría todos los índices siguientes. El código corta
por el **último** `)` y cuenta los campos a partir de ahí (por eso `tokens[N]`
corresponde al campo del núcleo `N + 3`). Ver
[03-backend-linux.md](03-backend-linux.md).

---

### ¿Cómo viajan los datos dentro de la aplicación?

```
/proc → LinuxProcessSource → ProcessInfo → CpuSampler::apply
      → Monitor::visible_ → TableController::render → Label
```

En palabras: el backend lee `/proc` y arma una lista de `ProcessInfo` con el
tiempo de CPU **acumulado**; el `CpuSampler` lo diferencia contra el refresco
anterior y rellena los porcentajes; el `Monitor` guarda el snapshot, lo filtra y
lo ordena en `visible_`; el `TableController` escribe la ventana visible en las
etiquetas; el motor las dibuja. Detalle en
[01-arquitectura.md](01-arquitectura.md#14-recorrido-de-un-dato).

---

### ¿Cómo se calcula el %CPU de un proceso?

Dividiendo su tiempo de CPU entre el tiempo total del sistema, en dos refrescos
consecutivos:

```
%CPU = (tiempo_actual_del_proceso − tiempo_anterior_del_proceso)
       ──────────────────────────────────────────────────────────── × 100
       (total_actual_del_sistema − total_anterior_del_sistema)
```

Es exactamente la fórmula que fija el enunciado. Lo hace el `CpuSampler`. Ver
[05-cpu-y-memoria.md](05-cpu-y-memoria.md).

---

### ¿De dónde sale el %CPU global del sistema?

Del mismo enfoque de dos refrescos, sobre la máquina completa:

```
%CPU_sys = (Δtotal − Δocioso) / Δtotal × 100
```

El tiempo ocioso se resta del total y lo que queda es lo ocupado. Lo calcula el
propio `CpuSampler::apply()` (y se lee con `system_cpu_percent()`); el `Monitor`
solo lo muestra en el resumen. Ver
[05-cpu-y-memoria.md](05-cpu-y-memoria.md).

---

### ¿Por qué el primer refresco muestra 0 % de CPU?

Porque no hay un refresco anterior con el que restar. Un porcentaje necesita dos
instantes; en el primero no hay nada que diferenciar, así que todos los procesos
informan `0 %`. Es la respuesta honesta. Ver
[05-cpu-y-memoria.md](05-cpu-y-memoria.md).

---

### ¿Cómo se calcula el %MEM?

`%MEM = rss_bytes × 100 / mem_total`. Se calcula siempre, en cada refresco, sin
necesidad del anterior.

---

### ¿Qué pasa si un proceso termina mientras se lee, o si no hay permiso?

Nada grave: es el caso normal de un monitor.

- Si un proceso desaparece entre una lectura y otra, se salta y se continúa; un
  vector vacío es una respuesta válida.
- Las acciones (`terminate`, `suspend`, `resume`, `set_priority`) devuelven
  `false` cuando el proceso ya no existe o no hay permiso, y la interfaz muestra
  el mensaje `PID <n>: failed (gone or no permission)`. Nunca se cae la
  aplicación (elemento 15 del enunciado).

Ver [02-capa-de-datos.md](02-capa-de-datos.md) y
[08-monitor-y-hotkeys.md](08-monitor-y-hotkeys.md).

---

### ¿Por qué no usa `ps`, `top` ni bibliotecas como `procfs`, `sysinfo` o `libgtop`?

Porque el enunciado lo prohíbe expresamente: todos los datos deben venir de la
interfaz del sistema operativo, y los porcentajes, totales y el resumen deben
calcularse en el código. `tasks-lens` no ejecuta ningún programa externo ni usa
bibliotecas que devuelvan los procesos o los porcentajes ya resueltos.

---

### ¿Cómo funciona la interfaz?

Es un árbol de nodos (grafo de escena). La pantalla se arma en `build_scene()`:
un panel a pantalla completa contiene un `VBox` con las filas (título, búsqueda,
dos de resumen, acciones, tabla y ayuda). La lógica se reparte en dos objetos
asociados a nodos:

- **`TableController`** (en el panel de la tabla): orden, selección y ventana de
  filas.
- **`Monitor`** (en la raíz): datos, atajos, acciones y vistas de detalle.

El motor llama a los scripts en momentos conocidos: al asociarlos, en cada
fotograma y ante cada evento de entrada. Ver
[06-interfaz-tui.md](06-interfaz-tui.md) y
[01-arquitectura.md](01-arquitectura.md).

---

### ¿Cómo se ordena la tabla y por qué la selección no "salta"?

Se ordena por la columna activa, y se cambia con `s` o haciendo clic en una
cabecera; volver a elegir la misma columna invierte la dirección. El orden
inicial es por **PID ascendente**, no por %CPU: el %CPU reordena la tabla varias
veces por segundo porque los deltas oscilan, y eso se percibe como parpadeo.

La selección se guarda por **PID**, no por índice. Como el índice cambia al
reordenar, guardar el PID hace que el cursor siga apuntando al mismo proceso.
Ver [07-tabla-y-controlador.md](07-tabla-y-controlador.md).

---

### ¿Cómo se distinguen los zombies y los procesos bloqueados en E/S?

Por el estado y el color de la fila. Un zombie (`Z` o `X`) se pinta en rojo
brillante; un proceso dormido sin interrupción (`D`, típicamente esperando E/S)
se pinta en amarillo brillante. El resto, en blanco. Ver
[07-tabla-y-controlador.md](07-tabla-y-controlador.md).

---

### ¿Cómo se cambia la prioridad y qué significan los números?

Con `p` (o el botón **Nice**) sobre el proceso seleccionado. En Linux la
prioridad es el valor **nice**, de `-20` (más prioridad) a `19` (menos
prioridad); el valor se recorta a ese rango. El campo se deja vacío a propósito
para no editar sobre el valor actual.

En Windows el mismo campo contiene el valor de `GetPriorityClass`, que **no es
comparable** con el nice de Linux. Ver
[08-monitor-y-hotkeys.md](08-monitor-y-hotkeys.md) y
[04-backend-windows.md](04-backend-windows.md).

---

### ¿Cómo se ven el árbol de procesos, los hilos y el historial?

Con las teclas `r` (árbol), `t` (hilos) y `g` (historial) sobre el proceso
seleccionado, o con sus botones. Cada vista se abre como un panel a pantalla
completa por encima de la tabla, y se cierra con `Escape` o el botón **Close**.

- El **árbol** muestra la jerarquía padre → hijo de toda la máquina.
- Los **hilos** listan cada hilo del proceso con su estado y su tiempo de CPU.
- El **historial** dibuja la serie de %CPU del proceso (hasta 120 muestras).

Ver [08-monitor-y-hotkeys.md](08-monitor-y-hotkeys.md).

---

### ¿Qué pasa si hay cientos de procesos? ¿Se crea un nodo por proceso?

No. La tabla usa **virtualización**: por cada columna existe un pool de etiquetas
del tamaño de la ventana visible, y cada fotograma se escribe en ellas el
segmento que se ve. Así el número de nodos depende de la altura del terminal, no
del número de procesos. Ver
[07-tabla-y-controlador.md](07-tabla-y-controlador.md).

---

### ¿Por qué el comando del proceso seleccionado se desplaza solo?

Porque puede ser más ancho que su columna, y en vez de cortarlo se muestra como
una **marquesina** que lo desplaza horizontalmente y vuelve al inicio. Se mide en
puntos de código, así que un carácter multibyte nunca se parte. Ver
[09-formatos-y-estilos.md](09-formatos-y-estilos.md).

---

### ¿Cómo cambio la velocidad de refresco?

Con `i` o el botón **Interval**, que cicla entre 1, 2, 5 y 10 segundos. El
intervalo actual aparece en el resumen y en el propio botón. Los datos se releen
a ese ritmo, no en cada fotograma.

---

### ¿Por qué a veces todo aparece centrado?

Porque `Container`, `VBox` y `HBox` centran el bloque de sus hijos por defecto
(`ContentAlignment::Center`). Una columna o fila que ocupe toda la pantalla
queda centrada salvo que se indique `ContentAlignment::Begin`. Por eso todas las
filas de la pantalla usan `Begin`. Ver
[06-interfaz-tui.md](06-interfaz-tui.md).

---

### ¿Qué es un "Script"?

Es un objeto de lógica que el motor asocia a un nodo y al que llama en momentos
conocidos: al asociarlo (`on_attached`), en cada fotograma (`on_process`) y ante
cada evento de entrada (`on_input`). El programa usa dos: `TableController` y
`Monitor`. Ver [01-arquitectura.md](01-arquitectura.md).

---

### ¿Cómo se adapta a un cambio de tamaño del terminal?

Todo el cálculo de cuántas filas caben y del ancho de la columna flexible se
rehace cada fotograma a partir del tamaño real del terminal, y solo actúa si el
resultado cambió. La tabla se redimensiona sola sin reconstruir el árbol. Ver
[07-tabla-y-controlador.md](07-tabla-y-controlador.md).

---

### ¿Por qué hay dos backends?

Porque el programa debe ser nativo del sistema donde corre. La interfaz
`ProcessSource` separa la lógica (común) de la lectura del sistema (propia de
cada uno). La fábrica elige el backend en tiempo de compilación: `/proc` en
Linux y las API Tool Help / PSAPI en Windows. Ver
[02-capa-de-datos.md](02-capa-de-datos.md) y
[04-backend-windows.md](04-backend-windows.md).

---

### ¿Cómo verifico que funciona sin abrir la interfaz?

Con el modo de volcado: `./build/tasks-lens --dump`. Toma dos instantáneas
separadas por un segundo y muestra por `stdout` un resumen del sistema y los diez
procesos con más CPU. No toma el terminal, así que se puede usar en una tubería.
Para verificar el layout del motor hay un binario de pruebas headless. Ver
[10-compilacion-y-ejecucion.md](10-compilacion-y-ejecucion.md).

---

### ¿Qué herramientas de compilación necesita?

CMake 3.14 o superior y un compilador de C++17. El motor es un submódulo de git
en `external/GuayabaTUIEngine`. La compilación habitual es `cmake --build build`.
Ver [10-compilacion-y-ejecucion.md](10-compilacion-y-ejecucion.md).
