#ifndef TASKS_LENS_DATA_PROCESS_SOURCE_H
#define TASKS_LENS_DATA_PROCESS_SOURCE_H

#include <memory>
#include <vector>

#include "Data/process_info.h"

namespace tasks_lens {

/*
    The platform seam. One implementation per operating system, chosen by
    create_system_process_source(), exactly the way GuayabaTUIEngine picks a
    terminal backend. Everything above this line is OS-agnostic.

    Read methods return a fresh snapshot; cpu_percent/mem_percent are left at 0
    on purpose and filled by CpuSampler, which owns the previous-refresh state.

    Action methods return false on failure (process gone, no permission) rather
    than throwing, because a vanished process is the normal case for a monitor
    and spec item 15 asks for a message, not a crash.
*/
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

std::unique_ptr<ProcessSource> create_system_process_source();

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PROCESS_SOURCE_H
