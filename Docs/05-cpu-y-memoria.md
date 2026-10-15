# 05. CPU y memoria

Este documento describe los cálculos. Todos viven en código propio; no se usa
ninguna biblioteca que devuelva porcentajes ya resueltos.

## 5.1. La idea de base: contadores acumulados

El sistema operativo no entrega "porcentaje de CPU"; entrega **tiempo de CPU
acumulado** desde que el proceso arrancó. Un porcentaje es siempre una
diferencia entre dos instantes. Por eso el programa guarda el estado del refresco
anterior y, en cada refresco nuevo, divide:

```
             Δtiempo_del_proceso
%CPU  =  ───────────────────────  × 100
            Δtiempo_del_sistema
```

Esta es exactamente la fórmula que fija el enunciado para el %CPU propio.

## 5.2. El `CpuSampler` (`src/Data/cpu_sampler.h` y `.cpp`)

Es el único lugar donde se hacen estas cuentas. Su estado es únicamente el
refresco previo, indexado por PID:

```cpp
std::uint64_t previous_total_cpu_ns_ = 0;
bool has_previous_total_ = false;
std::unordered_map<int, std::uint64_t> previous_cpu_ns_;
```

El método `apply(system, processes)` hace, para cada proceso:

1. Calcula `total_delta = system.total_cpu_time_ns - previous_total_cpu_ns_`,
   pero solo si el total **avanzó** respecto del refresco anterior.
2. Si hay delta válido y el proceso existía antes con un tiempo menor, calcula:
   ```
   cpu_percent = (cpu_time_ns - cpu_anterior) × 100 / total_delta
   ```
3. Calcula siempre:
   ```
   mem_percent = rss_bytes × 100 / mem_total
   ```
4. Guarda el snapshot actual como nuevo punto de referencia.

### Casos especiales

- **Primer refresco:** no hay anterior, así que todos los procesos informan
  `0 %`. Es lo honesto: todavía no hay nada que restar.
- **Contador que retrocede o no avanza:** si `total_cpu_time_ns` no avanzó (o
  retrocedió, por ejemplo por un reinicio de contador), no hay denominador
  válido y todos los `cpu_percent` quedan en `0` en lugar de dividir por cero.
- **Proceso nuevo:** si no estaba en el mapa previo, su `cpu_percent` es `0`
  durante ese refresco.
- **`mem_total` cero:** `mem_percent` queda en `0` en lugar de dividir por cero.

Un punto importante de diseño: el backend solo aporta `cpu_time_ns` acumulado;
la memoria del refresco anterior vive en el `CpuSampler`, de modo que ningún
backend repite esta matemática.

## 5.3. %CPU global del sistema

El resumen muestra un único porcentaje de CPU para toda la máquina. El
`CpuSampler` no lo calcula; lo calcula el `Monitor::refresh()`
(`src/Scripts/monitor.h`, líneas 304-317) con los dos contadores de `SystemInfo`:

```
             (Δtotal − Δidle)
%CPU_sys  =  ──────────────────  × 100
                  Δtotal
```

Es el mismo enfoque de dos refrescos, pero sobre la máquina completa: el tiempo
ocioso se resta del total y lo que queda es el tiempo realmente ocupado. El
denominador es el mismo `total_cpu_time_ns` que usa el %CPU por proceso, lo que
hace que ambos sean consistentes.

## 5.4. De dónde sale cada contador

| Valor | Linux | Windows |
|---|---|---|
| `cpu_time_ns` del proceso | `utime + stime` de `/proc/<pid>/stat`, en jiffies → ns | `kernel + user` de `GetProcessTimes`, en `FILETIME` → ns |
| `total_cpu_time_ns` | suma de los 8 primeros campos de la primera línea de `/proc/stat` → ns | `kernel + user` de `GetSystemTimes` → ns |
| `idle_cpu_time_ns` | `idle + iowait` de `/proc/stat` → ns | `idle` de `GetSystemTimes` → ns |
| `mem_total` / `mem_available` | `/proc/meminfo` (kB → bytes) | `GlobalMemoryStatusEx` (bytes) |
| `rss_bytes` | campo 24 de `stat` × tamaño de página | `WorkingSetSize` de `GetProcessMemoryInfo` |

En los dos sistemas, la unidad final es el nanosegundo, y por eso el mismo
`CpuSampler` funciona sin saber en qué plataforma corre.

## 5.5. Historial de CPU por proceso (elemento 7 del enunciado)

El `Monitor` mantiene una serie por proceso para la vista de historial:

```cpp
static constexpr std::size_t kHistorySamples = 120;
std::unordered_map<int, std::deque<double>> history_;
```

`record_history()` (líneas 329-345) se llama en cada refresco: agrega el
`cpu_percent` actual a la serie del PID y recorta la serie a `kHistorySamples`
(120 muestras). La clave es el **PID**, no la posición, para que la serie
sobreviva a los reordenamientos de la tabla. Los procesos que ya no existen se
eliminan del mapa.

Con un intervalo de refresco de 1 segundo, 120 muestras son dos minutos de
historial. La vista de historial dibuja la serie en un `Graph` con
autoescalado: como un solo proceso suele estar cerca del 0 % de la máquina
entera, un rango fijo de 0 a 100 aplanaría su variación por debajo de una celda.

Véase también: [03-backend-linux.md](03-backend-linux.md) para el origen de los
números y [07-tabla-y-controlador.md](07-tabla-y-controlador.md) para cómo se
muestran.
