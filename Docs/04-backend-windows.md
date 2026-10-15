# 04. Backend de Windows

Implementado en
`src/Data/platform/windows/windows_process_source.cpp`. Se compila solo en
Windows (`#ifdef _WIN32`) y usa las API de Tool Help (`tlhelp32.h`) y PSAPI
(`psapi.h`).

> Nota de estado: en este repositorio el backend de Windows está **compilado y
> estructurado**, pero sus cuatro acciones (`terminate`, `suspend`, `resume`,
> `set_priority`) están sin implementar y siempre devuelven `false`. La lectura
> de datos está completa.

## 4.1. Equivalencias con la interfaz `ProcessSource`

| Método | API de Windows |
|---|---|
| `read_system()` | `GetSystemTimes`, `GlobalMemoryStatusEx`, `CreateToolhelp32Snapshot` |
| `read_processes()` | `CreateToolhelp32Snapshot`, `OpenProcess`, `GetProcessTimes`, `GetProcessMemoryInfo`, `GetPriorityClass`, `QueryFullProcessImageNameW`, `OpenProcessToken` |
| `read_threads(pid)` | `CreateToolhelp32Snapshot` (`TH32CS_SNAPTHREAD`) |
| `terminate/suspend/resume` | (sin implementar; previsto `TerminateProcess`, `SuspendThread`/`ResumeThread`) |
| `set_priority` | (sin implementar; previsto `SetPriorityClass`) |

## 4.2. Normalización de unidades

La misma regla que en Linux: todo el tiempo de CPU se lleva a nanosegundos.

- **`filetime_to_ns(FILETIME)`** (líneas 25-30): `FILETIME` cuenta intervalos de
  100 ns, así que basta multiplicar por `100`. Tras esto, un `FILETIME` y un
  contador de `/proc` comparten unidad, y el mismo `CpuSampler` sirve para
  ambos.
- **`wide_to_utf8`** (líneas 32-42): convierte `wchar_t` a UTF-8 con
  `WideCharToMultiByte`, para que los nombres sean `std::string` como en el
  resto del programa.

## 4.3. `read_system` (líneas 82-116)

- **`GetSystemTimes`** devuelve idle, kernel y user. Detalle importante (línea
  90): el tiempo de núcleo **ya incluye** el tiempo ocioso, así que
  `total = kernel + user` e `idle` se toma aparte. Así el %CPU global usa la
  misma fórmula que en Linux.
- **`GlobalMemoryStatusEx`** da memoria total, disponible y (derivada) usada.
- **`CreateToolhelp32Snapshot`** recorre los procesos solo para contarlos:
  `info.total++`.
- **`load_avg` y los conteos por estado quedan en sus valores por defecto.** No
  hay equivalente directo en Windows; el código, a propósito, no inventa
  números.

## 4.4. `read_processes` (líneas 118-167)

Un recorrido con `Process32FirstW` / `Process32NextW`. De la entrada `PROCESSENTRY32W`
salen, sin abrir el proceso, `pid`, `ppid`, `thread_count` y `command` (el
nombre del ejecutable).

Después, `OpenProcess` con `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`.
Si tiene éxito, se obtienen:

- `cpu_time_ns = kernel + user` (de `GetProcessTimes`), otra vez en el mismo
  formato acumulado que en Linux.
- `rss_bytes = counters.WorkingSetSize` (de `GetProcessMemoryInfo`).
- `priority = GetPriorityClass(process)`.
- `command`, si `QueryFullProcessImageNameW` da la ruta de la imagen.
- `user`, con `OpenProcessToken` + `GetTokenInformation(TokenUser)` +
  `LookupAccountSidW`.

Si `OpenProcess` falla (procesos protegidos), solo quedan los campos que da la
instantánea. El proceso se agrega igualmente.

## 4.5. `read_threads` (líneas 169-190)

Recorre la instantánea de hilos y conserva los que pertenecen al proceso pedido
(`th32OwnerProcessID == pid`). Detalle a tener en cuenta: la instantánea **no
expone el estado** de un hilo, así que cada hilo se marca como
`ProcessState::Running` (un hilo listado es uno que existe).

## 4.6. Diferencias de semántica respecto de Linux

| Aspecto | Linux | Windows |
|---|---|---|
| Origen de datos | archivos de `/proc` | API del sistema |
| Tiempo de CPU | jiffies → ns | `FILETIME` (100 ns) → ns |
| `priority` | `nice`, de `-20` a `19` | `GetPriorityClass`, valor no comparable |
| `load_avg` | de `/proc/loadavg` | no disponible (queda en 0) |
| Estados por subcuenta | conteo real | no disponible (queda en 0) |
| Estado de un hilo | del `stat` del hilo | no disponible (se asume `Running`) |
| Acciones | implementadas | sin implementar |

Estas diferencias son la razón por la que la interfaz no debe asumir rangos
concretos de `priority`: el aviso de la línea de estado (`-20..19`) tiene forma
de Linux.
