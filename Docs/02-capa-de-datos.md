# 02. Capa de datos

Todo lo que está en `src/Data/` es independiente de la plataforma. La frontera
es la interfaz `ProcessSource`: por encima de ella no se ve ni una línea de
`/proc` ni una estructura de Win32.

## 2.1. Tipos neutrales (`src/Data/process_info.h`)

### `enum class ProcessState`

Representa el estado de un proceso o de un hilo con el mismo conjunto de valores
en todos los sistemas:

| Valor | Código | Significado |
|---|---|---|
| `Running` | `R` | En ejecución. |
| `Sleeping` | `S` | Dormido (interrumpible). |
| `DiskSleep` | `D` | Dormido sin interrupción, típicamente esperando E/S. |
| `Stopped` | `T` | Detenido (por señal o por depuración). |
| `Zombie` | `Z` | Terminado, aún no recolectado por su padre. |
| `Idle` | `I` | Inactivo (hilos ociosos del núcleo). |
| `Unknown` | `?` | No reconocido. |

La función `process_state_code(ProcessState)` devuelve la letra canónica,
siguiendo la convención de `ps` y `top`. Ese código es el que aparece en la
columna `STATE` y en la vista de hilos.

### `struct ProcessInfo`

Una fila de la tabla. Campos:

| Campo | Tipo | Origen |
|---|---|---|
| `pid` | `int` | El backend. |
| `ppid` | `int` | El backend. |
| `user` | `std::string` | El backend (nombre del usuario dueño). |
| `state` | `ProcessState` | El backend. |
| `cpu_percent` | `double` | **El `CpuSampler`**, no el backend. |
| `mem_percent` | `double` | **El `CpuSampler`** (según `rss_bytes` / memoria total). |
| `rss_bytes` | `uint64_t` | El backend, en bytes. |
| `thread_count` | `int` | El backend. |
| `priority` | `int` | El backend (`nice` en Linux). |
| `cpu_time_ns` | `uint64_t` | El backend, tiempo **acumulado** en nanosegundos. |
| `command` | `std::string` | El backend. |

La separación es deliberada: un backend solo informa `cpu_time_ns` acumulado,
porque un porcentaje requiere que exista un refresco previo. Los campos
`cpu_percent` y `mem_percent` los rellena el `CpuSampler` más adelante.

### `struct ThreadInfo`

Una fila de la vista de hilos: `tid`, `state`, `cpu_time_ns` y `name`.

### `struct SystemInfo`

El resumen de la máquina: `total`, `running`, `stopped`, `zombie`,
`load_avg[3]`, `mem_total`, `mem_used`, `mem_available`, `total_cpu_time_ns` e
`idle_cpu_time_ns`. Los dos últimos son el numerador y el denominador con los que
se calcula el %CPU global del sistema (ver
[05-cpu-y-memoria.md](05-cpu-y-memoria.md)).

## 2.2. La interfaz `ProcessSource` (`src/Data/process_source.h`)

```cpp
class ProcessSource {
 public:
  virtual ~ProcessSource() = default;

  virtual const char* platform_name() const = 0;

  virtual SystemInfo read_system() = 0;
  virtual std::vector<ProcessInfo> read_processes() = 0;
  virtual std::vector<ThreadInfo> read_threads(int pid) = 0;

  virtual bool terminate(int pid, bool force) = 0;
  virtual bool suspend(int pid) = 0;
  virtual bool resume(int pid) = 0;
  virtual bool set_priority(int pid, int priority) = 0;
};
```

Contratos que todo backend debe respetar:

- **Los métodos de lectura devuelven un snapshot nuevo.** `cpu_percent` y
  `mem_percent` se dejan a `0` a propósito; los rellena el `CpuSampler`.
- **Las acciones devuelven `bool`, nunca lanzan excepciones.** `false` significa
  "proceso inexistente" o "sin permiso". Un proceso que desaparece es el caso
  normal de un monitor, no un error: el enunciado pide un mensaje, no una caída.
- **La lectura no falla de forma catastrófica.** Si no se puede leer un archivo
  concreto, se salta ese proceso y se continúa; un vector vacío es una respuesta
  válida.

La declaración de la fábrica también vive aquí:

```cpp
std::unique_ptr<ProcessSource> create_system_process_source();
```

## 2.3. La fábrica (`src/Data/process_source_factory.cpp`)

El archivo incluye el encabezado del backend correcto con un `#if` y devuelve
una instancia concreta:

```cpp
#if defined(_WIN32)
  return std::make_unique<WindowsProcessSource>();
#elif defined(__linux__)
  return std::make_unique<LinuxProcessSource>();
#endif
```

Si la plataforma no es Windows ni Linux, `#error` detiene la compilación. El
mismo `#if` decide qué archivo `.cpp` entra en la compilación (ver
[10-compilacion-y-ejecucion.md](10-compilacion-y-ejecucion.md)).

## 2.4. Cómo encaja con la interfaz

- `main.cpp` llama a `create_system_process_source()` y le entrega el puntero al
  `Monitor`.
- El `Monitor` llama a `read_system()`, `read_processes()` y `read_threads()`
  según necesita, y a `terminate()`, `suspend()`, `resume()` y
  `set_priority()` cuando el usuario actúa.
- El `CpuSampler` (que también vive en `src/Data/`) recibe las estructuras y las
  completa con los porcentajes.

Ninguno de esos consumidores conoce la plataforma; solo ven la interfaz.
