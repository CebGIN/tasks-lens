# 03. Backend de Linux

Implementado en `src/Data/platform/linux/linux_process_source.cpp`. Lee el
sistema de archivos virtual `/proc` y traduce su contenido a los tipos neutrales
de `src/Data/process_info.h`.

## 3.1. Qué archivos se leen

| Archivo | Para qué | Dónde |
|---|---|---|
| `/proc/<pid>/stat` | pid, ppid, estado, tiempo de CPU, nice, hilos, RSS | `read_processes` y `read_threads` |
| `/proc/<pid>/status` | uid (para el nombre de usuario) | `read_processes` |
| `/proc/<pid>/cmdline` | columna `COMMAND` | `read_processes` |
| `/proc/<pid>/task/<tid>/stat` | estado y tiempo de cada hilo | `read_threads` |
| `/proc/stat` | tiempo total y ocioso del sistema | `read_system` |
| `/proc/loadavg` | carga media | `read_system` |
| `/proc/meminfo` | memoria total, disponible, libre, buffers, caché | `read_system` |

## 3.2. Primitivas (líneas 27-54)

- **`clock_ticks_per_sec()`** — devuelve `sysconf(_SC_CLK_TCK)` (habitualmente
  100). Se cachea en una variable `static` porque no cambia mientras el programa
  se ejecuta. Si falla, usa `100` como respaldo.
- **`page_size_bytes()`** — devuelve `sysconf(_SC_PAGESIZE)` (habitualmente
  4096), también cacheado, con `4096` como respaldo.
- **`ticks_to_ns(ticks)`** — convierte jiffies a nanosegundos. Es la conversión
  que hace intercambiables los backends: `ProcessInfo::cpu_time_ns` siempre está
  en nanosegundos.
- **`read_file(path, out)`** — lee el archivo completo en un `std::string` en
  modo binario (necesario porque `cmdline` contiene bytes nulos). Devuelve
  `false` si el archivo no se puede abrir, lo cual es el caso normal de un
  proceso que acaba de terminar.
- **`is_numeric_pid(name)`** — indica si un nombre de entrada de `/proc` es un
  PID válido. `/proc` mezcla PID con `self`, `meminfo`, `cpuinfo`, etc.; una
  entrada es un PID solo si **todos** sus caracteres son dígitos.

## 3.3. `parse_stat`: el análisis de `/proc/<pid>/stat` (líneas 72-108)

Esta es la función central. Un ejemplo real de la línea:

```
119249 (cat) R 119248 119248 119248 0 -1 4194304 91 0 0 0 0 0 0 0 20 0 1 0 3471501 6070272 439 ...
```

El núcleo numera los campos desde 1: el 1 es el pid, el 2 es `comm` (el nombre
del ejecutable, entre paréntesis), el 3 es el estado, y así hasta el 52.

### El problema

El campo 2, `comm`, puede contener **espacios y paréntesis**, porque el usuario
puede nombrar un ejecutable como quiera. Un proceso llamado `mi (raro) app`
produce una línea como:

```
1234 (mi (raro) app) R 11 ...
```

Un *split* ingenuo por espacios de toda la línea desplazaría todos los índices
siguientes y leería basura en `ppid`, `utime`, etc.

### La solución

El código corta la línea por el **último** `)` y analiza lo que sigue:

```cpp
const std::size_t lparen = data.find('(');   // primer '('  → inicio de comm
const std::size_t rparen = data.rfind(')');  // último ')'  → fin de comm
f.comm = data.substr(lparen + 1, rparen - lparen - 1);

std::istringstream ss(data.substr(rparen + 1));  // todo lo que sigue
```

`rfind(')')` es correcto precisamente porque **ningún campo posterior a `comm`
puede contener un `)`**: son todos números o códigos de un carácter. Por eso el
último `)` de la línea es, sin ambigüedad, el final de `comm`.

### El reindexado

Tras el corte, `tokens[0]` es el campo 3 (no el 1). La conversión es
`tokens[N - 3]`. Campos que usa el programa:

| En el código | Campo del núcleo | Contenido |
|---|---|---|
| `tokens[0]` | 3 | estado (`R`, `S`, `D`, `T`, `Z`, ...) |
| `tokens[1]` | 4 | ppid |
| `tokens[11]` | 14 | `utime` (tiempo de CPU en modo usuario) |
| `tokens[12]` | 15 | `stime` (tiempo de CPU en modo núcleo) |
| `tokens[16]` | 19 | nice (prioridad) |
| `tokens[17]` | 20 | número de hilos |
| `tokens[21]` | 24 | RSS (en páginas) |

La comprobación `if (tokens.size() < 22) return false;` corta las lecturas
truncadas o en carrera: una línea bien formada tiene 50 tokens (campos 3 a 52),
así que 22 es el mínimo para llegar a `rss` (índice 21).

La estructura `StatFields` lleva el número de campo del núcleo como comentario
en cada miembro; es la convención a mantener si se agrega un campo nuevo.

## 3.4. `map_state` (líneas 56-68)

Traduce la letra de estado a `ProcessState`. Dos equivalencias no son obvias:

- `'T'` **y** `'t'` → `Stopped`. La `t` minúscula es una detención por
  depuración; a efectos de la interfaz está detenido igual.
- `'Z'` **y** `'X'` → `Zombie`. `Z` es "zombie" y `X` es "muerto"; ambos
  significan terminado sin recolectar, que es lo que hay que resaltar.

No hay una letra aparte para "bloqueado en E/S": ese es el estado `D`
(`DiskSleep`), y es el `TableController` el que lo pinta de amarillo.

## 3.5. `read_system` (líneas 239-251)

`read_system` delega en cuatro ayudantes locales; si una lectura falla, las otras
siguen funcionando.

### `/proc/stat` — `read_cpu_times` (líneas 154-172)

Primera línea:

```
cpu  1213278 10725 292589 11775356 71059 0 7799 0 0 0
     user    nice  system   idle   iowait ...
```

Se suman los primeros 8 valores en `ticks`, y por separado se acumulan los
índices 3 (`idle`) y 4 (`iowait`) en `idle_ticks`. Dos detalles:

- El bucle se detiene en 8 campos porque `guest` y `guest_nice` (campos 9 y 10)
  **ya están incluidos** dentro de `user`/`nice`; sumarlos contaría doble.
- Se cuentan tanto `idle` como `iowait`, porque una máquina bloqueada en E/S no
  está ociosa para la CPU, pero sus jiffies caen en `iowait`.

El resultado va a `total_cpu_time_ns` e `idle_cpu_time_ns`.

### Conteo de estados — `read_task_counts` (líneas 175-196)

Un segundo recorrido de `/proc` que lee solo `stat` y cuenta `total`, `running`,
`stopped` y `zombie`. Es una pasada **separada** de `read_processes`, así que el
resumen y la tabla pueden diferir en un proceso o dos si uno termina entre una y
otra. El `switch` no cuenta `Sleeping` ni `DiskSleep` ni `Idle`: están en
`total`, pero no en ninguna subcuenta.

### `/proc/loadavg` — `read_load_avg` (líneas 198-203)

Tres números separados por espacios, directos a `load_avg[3]`.

### `/proc/meminfo` — `read_memory` (líneas 205-235)

Un recorrido línea a línea que toma `MemTotal`, `MemFree`, `MemAvailable`,
`Buffers` y `Cached`. Dos decisiones:

- **Respaldo (línea 231):** si `MemAvailable` es `0` (núcleos antiguos a los que
  les falta el campo), se calcula como `mem_free + buffers + cached`.
- **`used` se deriva, nunca se lee (líneas 232-234):**
  `mem_used = mem_total - mem_available`. El núcleo no expone `MemUsed`, y
  calcularlo como `total - free` ignoraría la caché de páginas.

Los valores de `meminfo` están en **kB**: se multiplican por `1024` para
guardarlos en bytes, igual que el resto de los campos.

## 3.6. `read_processes` (líneas 253-300)

Recorre las entradas de `/proc`, salta las que no son PID y, para cada una:

1. **`stat`** → todos los campos de `parse_stat`. De ahí:
   - `cpu_time_ns = ticks_to_ns(utime + stime)`. La suma de usuario más sistema
     es el tiempo de CPU **acumulado**; es el número en bruto que el
     `CpuSampler` diferenciará contra el refresco anterior. No se incluyen
     `cutime`/`cstime` (el tiempo de los hijos), que sería incorrecto atribuir al
     proceso.
   - `rss_bytes = rss * page_size_bytes()`. El campo 24 está en **páginas**.
     La guarda `rss > 0` evita convertir en un número gigante un valor negativo
     de un proceso que está terminando.
   - `priority = nice`.
2. **`status` → uid (líneas 283-287 y `read_uid`, 110-124).** Este archivo no se
   analiza por espacios: es un bloque de `Clave:\tvalor`, y el código busca el
   literal `"\nUid:"`. Luego lee el **primer** número de esa línea (el UID real);
   los otros tres (efectivo, guardado, de sistema de archivos) se ignoran.
3. **`cmdline` → command (líneas 289-292).** `normalize_cmdline` (líneas
   145-151) reemplaza cada byte nulo (`\0`) por un espacio y elimina los espacios
   finales. El archivo es el vector `argv` en crudo, separado por NUL, no una
   línea de texto; sin esta conversión cada cadena se cortaría en el primer NUL.
4. **Respaldo para hilos del núcleo (línea 293):**
   `if (proc.command.empty()) proc.command = fields.comm;`. Un hilo del núcleo
   tiene `cmdline` **vacío** (no tiene `argv`), así que se usa `comm` de `stat`
   — de ahí salen nombres como `[kthreadd]` o `[kworker/0:1]`.

## 3.7. `username` y la caché de usuarios (líneas 127-142)

`username(uid, cache)` usa `getpwuid_r` (variante reentrante) y **cachea** el
resultado por uid en el `std::unordered_map` que recibe. La caché es un miembro
privado del backend, `user_cache_` (`linux_process_source.h`), que vive lo que
vive la fuente; `read_processes` le pasa la referencia. La razón: en una máquina
ociosa muchos procesos pertenecen a `root`, y cada `getpwuid` puede consultar
servicios externos (NSS). Sin la caché serían decenas de consultas por refresco.
Si la consulta falla, se guarda el uid numérico como nombre, de modo que la
columna `USER` muestra `1000` en lugar de quedar vacía.

## 3.8. `read_threads` (líneas 302-328)

Es una copia reducida de `read_processes`:

- El directorio es `/proc/<pid>/task`, y las entradas son **TID** (también
  numéricos, por lo que el mismo filtro sirve).
- Se analiza cada `stat` con el **mismo `parse_stat`**: el `stat` de un hilo
  tiene exactamente los mismos 52 campos.
- Solo se rellenan `tid`, `state`, `cpu_time_ns` y `name` (de `comm`). No se lee
  `status` ni `cmdline`: los hilos no tienen `cmdline`, y leerlos por hilo sería
  abrir archivos de más para datos que la interfaz no muestra.
- Si el proceso ya no existe, `opendir` falla y se devuelve un vector vacío; no
  es un error.

## 3.9. Acciones (líneas 330-344)

Todas usan llamadas de sistema; no leen nada de `/proc`:

```cpp
terminate(pid, force) → ::kill(pid, force ? SIGKILL : SIGTERM)
suspend(pid)          → ::kill(pid, SIGSTOP)
resume(pid)           → ::kill(pid, SIGCONT)
set_priority(pid, p)  → ::setpriority(PRIO_PROCESS, pid, p)
```

Cada una devuelve si la llamada tuvo éxito (`== 0`). Un `false` significa
`EPERM` (sin permiso) o `ESRCH` (el proceso ya no existe); la interfaz lo
convierte en un mensaje, nunca en una caída.

## 3.10. Puntos delicados

- **El truco de `rfind(')')` es esencial.** Cualquier "limpieza" que lo cambie
  por un `find` o por un *split* de la línea entera corrompe silenciosamente
  todos los campos de todo proceso cuyo nombre contenga un `)`.
- **`priority` no es el mismo concepto entre plataformas.** Aquí es `nice`
  (campo 19, de `-20` a `19`); en Windows es el valor de
  `GetPriorityClass`, que no es comparable. Ver
  [04-backend-windows.md](04-backend-windows.md).
