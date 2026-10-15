#ifndef TASKS_LENS_DATA_PROCESS_INFO_H
#define TASKS_LENS_DATA_PROCESS_INFO_H

#include <cstdint>
#include <string>

namespace tasks_lens {

// Platform-neutral snapshot types filled by every backend. cpu_time_ns is
// always nanoseconds of accumulated CPU time, the same unit as
// SystemInfo::total_cpu_time_ns.

enum class ProcessState {
  Running,
  Sleeping,
  DiskSleep,
  Stopped,
  Zombie,
  Idle,
  Unknown,
};

// Returns the one-letter state code used by ps/top; Unknown returns "?".
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

  // Set by CpuSampler (a backend only reports the cumulative cpu_time_ns).
  double cpu_percent = 0.0;
  double mem_percent = 0.0;

  std::uint64_t rss_bytes = 0;
  int thread_count = 0;

  // Scheduling priority: nice (-20..19) on Linux, GetPriorityClass on Windows.
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

  // Cumulative idle time (Linux idle+iowait, Windows idle).
  std::uint64_t idle_cpu_time_ns = 0;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PROCESS_INFO_H
