#include "Data/process_source.h"

#if defined(_WIN32)
    #include "Data/platform/windows/windows_process_source.h"
#elif defined(__linux__)
    #include "Data/platform/linux/linux_process_source.h"
#else
    #error "tasks-lens supports Windows and Linux."
#endif

namespace tasks_lens {

std::unique_ptr<ProcessSource> create_system_process_source() {
#if defined(_WIN32)
  return std::make_unique<WindowsProcessSource>();
#elif defined(__linux__)
  return std::make_unique<LinuxProcessSource>();
#endif
}

}  // namespace tasks_lens
