#ifndef TASKS_LENS_DATA_CPU_SAMPLER_H
#define TASKS_LENS_DATA_CPU_SAMPLER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Data/process_info.h"

namespace tasks_lens {

/*
    Turns cumulative counters into between-refresh percentages.

    The spec fixes the formula in one sentence: take each process's CPU time and
    the system's total CPU time on two consecutive refreshes, and divide the
    process difference by the system difference. cpu_time_ns is already in a
    common unit (nanoseconds) for every platform, so this is the whole of the
    platform-independent math and no backend repeats it.

    State is the previous snapshot, keyed by pid. The first call has no
    predecessor, so every process reports 0% - which is honest: there is nothing
    to subtract yet.
*/
class CpuSampler {
 public:
  // Fills cpu_percent and mem_percent in `processes` from the deltas against
  // the previous call, then records this call as the new baseline.
  void apply(SystemInfo& system, std::vector<ProcessInfo>& processes);

 private:
  std::uint64_t previous_total_cpu_ns_ = 0;
  bool has_previous_total_ = false;
  std::unordered_map<int, std::uint64_t> previous_cpu_ns_;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_CPU_SAMPLER_H
