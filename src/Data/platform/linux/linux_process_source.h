#ifndef TASKS_LENS_DATA_PLATFORM_LINUX_PROCESS_SOURCE_H
#define TASKS_LENS_DATA_PLATFORM_LINUX_PROCESS_SOURCE_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "Data/process_source.h"

namespace tasks_lens {

// Reads /proc directly (no ps, no procps).
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

 private:
  // Passwd lookups are slow, so resolved names are cached per uid for the
  // lifetime of the source.
  std::unordered_map<std::uint64_t, std::string> user_cache_;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PLATFORM_LINUX_PROCESS_SOURCE_H
