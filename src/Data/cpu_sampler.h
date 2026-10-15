#ifndef TASKS_LENS_DATA_CPU_SAMPLER_H
#define TASKS_LENS_DATA_CPU_SAMPLER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Data/process_info.h"

namespace tasks_lens {

// Turns cumulative CPU counters into between-refresh percentages.
class CpuSampler {
 public:
  // Fills cpu_percent/mem_percent in `processes` and system_cpu_percent() from
  // the deltas against the previous call, then records this call as baseline.
  void apply(SystemInfo& system, std::vector<ProcessInfo>& processes);

  // Whole-machine CPU usage (%) from the last apply; 0 before the second call.
  double system_cpu_percent() const { return system_cpu_percent_; }

 private:
  std::uint64_t previous_total_cpu_ns_ = 0;
  std::uint64_t previous_idle_cpu_ns_ = 0;
  bool has_previous_total_ = false;
  double system_cpu_percent_ = 0.0;
  std::unordered_map<int, std::uint64_t> previous_cpu_ns_;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_CPU_SAMPLER_H
