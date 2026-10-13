#ifndef TASKS_LENS_DATA_PLATFORM_LINUX_PROCESS_SOURCE_H
#define TASKS_LENS_DATA_PLATFORM_LINUX_PROCESS_SOURCE_H

#include "Data/process_source.h"

namespace tasks_lens {

// Reads /proc directly. No ps, no procps, no gopsutil: this is the interface the
// kernel exposes for exactly this purpose, and the spec names it as the source.
class LinuxProcessSource : public ProcessSource {
 public:
  const char* platform_name() const override { return "linux"; }

  SystemInfo read_system() override;
  std::vector<ProcessInfo> read_processes() override;
  std::vector<ThreadInfo> read_threads(int pid) override;

  bool terminate(int pid, bool force) override;
  bool suspend(int pid) override;
  bool resume(int pid) override;
  bool set_priority(int pid, int priority) override;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PLATFORM_LINUX_PROCESS_SOURCE_H
