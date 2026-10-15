#ifndef TASKS_LENS_DATA_PROCESS_SOURCE_H
#define TASKS_LENS_DATA_PROCESS_SOURCE_H

#include <memory>
#include <vector>

#include "Data/process_info.h"

namespace tasks_lens {

// Platform seam: one implementation per OS, picked by
// create_system_process_source(). Read methods return a fresh snapshot with
// cpu_percent/mem_percent left at 0; action methods return false on failure.
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
