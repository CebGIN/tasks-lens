#ifndef TASKS_LENS_DATA_PROCESS_INFO_H
#define TASKS_LENS_DATA_PROCESS_INFO_H

#include <cstdint>
#include <string>

namespace tasks_lens {

/*
    Platform-neutral snapshot types.

    Every backend fills these, so nothing above the ProcessSource interface ever
    sees a /proc line or a Win32 structure. The one thing that could have leaked
    through - how each OS counts CPU time - is normalized away here: cpu_time_ns
    is always nanoseconds of accumulated CPU time, and SystemInfo's
    total_cpu_time_ns is the same unit for the whole machine. That is what lets
    one CpuSampler do the delta math for every platform.
*/

enum class ProcessState {
  Running,
  Sleeping,
  DiskSleep,
  Stopped,
  Zombie,
  Idle,
  Unknown,
};

// Canonical one-letter code, matching the convention ps/top use (R S D T Z I).
inline const char* process_state_code(ProcessState state) {
  switch (state) {
    case ProcessState::Running: return "R";
    case ProcessState::Sleeping: return "S";
    case ProcessState::DiskSleep: return "D";
    case ProcessState::Stopped: return "T";
    case ProcessState::Zombie: return "Z";
    case ProcessState::Idle: return "I";
    case ProcessState::Unknown: break;
  }
  return "?";
}

struct ProcessInfo {
  int pid = 0;
  int ppid = 0;
  std::string user;
  ProcessState state = ProcessState::Unknown;

  // Filled by CpuSampler, not by the backend: a backend only reports cumulative
  // cpu_time_ns, because a percentage needs the previous refresh to exist.
  double cpu_percent = 0.0;
  double mem_percent = 0.0;

  std::uint64_t rss_bytes = 0;
  int thread_count = 0;

  // Scheduling priority. Linux stores nice (-20..19); Windows stores the
  // GetPriorityClass value. Both are directly settable through set_priority.
  int priority = 0;

  std::uint64_t cpu_time_ns = 0;
  std::string command;
};

struct ThreadInfo {
  int tid = 0;
  ProcessState state = ProcessState::Unknown;
  std::uint64_t cpu_time_ns = 0;
  std::string name;
};

struct SystemInfo {
  int total = 0;
  int running = 0;
  int stopped = 0;
  int zombie = 0;
  double load_avg[3] = {0.0, 0.0, 0.0};
  std::uint64_t mem_total = 0;
  std::uint64_t mem_used = 0;
  std::uint64_t mem_available = 0;
  std::uint64_t total_cpu_time_ns = 0;

  // Cumulative idle time (Linux idle+iowait, Windows idle). Kept so the overall
  // CPU percentage is (delta total - delta idle) / delta total, the same
  // two-refresh idea as a process, over the whole machine.
  std::uint64_t idle_cpu_time_ns = 0;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PROCESS_INFO_H
