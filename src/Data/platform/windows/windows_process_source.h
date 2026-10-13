#ifndef TASKS_LENS_DATA_PLATFORM_WINDOWS_PROCESS_SOURCE_H
#define TASKS_LENS_DATA_PLATFORM_WINDOWS_PROCESS_SOURCE_H

#include "Data/process_source.h"

namespace tasks_lens {

// Reads process data through Tool Help and PSAPI - the interfaces the spec names
// for Windows. Written but not yet compiled anywhere: it only builds on a
// Windows toolchain. Actions are intentionally stubbed until a Windows box can
// verify them.
class WindowsProcessSource : public ProcessSource {
 public:
  const char* platform_name() const override { return "windows"; }

  SystemInfo read_system() override;
  std::vector<ProcessInfo> read_processes() override;
  std::vector<ThreadInfo> read_threads(int pid) override;

  bool terminate(int pid, bool force) override;
  bool suspend(int pid) override;
  bool resume(int pid) override;
  bool set_priority(int pid, int priority) override;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PLATFORM_WINDOWS_PROCESS_SOURCE_H
