#include "Data/cpu_sampler.h"

namespace tasks_lens {

void CpuSampler::apply(SystemInfo& system, std::vector<ProcessInfo>& processes) {
  const std::uint64_t total_ns = system.total_cpu_time_ns;

  // A total that did not advance (or went backwards, e.g. a counter reset) has
  // no meaningful denominator, so leave every cpu_percent at 0 rather than
  // dividing by it.
  std::uint64_t total_delta = 0;
  const bool have_total_delta =
      has_previous_total_ && total_ns > previous_total_cpu_ns_;
  if (have_total_delta) total_delta = total_ns - previous_total_cpu_ns_;

  std::unordered_map<int, std::uint64_t> next_cpu_ns;
  next_cpu_ns.reserve(processes.size());

  for (ProcessInfo& proc : processes) {
    next_cpu_ns[proc.pid] = proc.cpu_time_ns;

    auto prev = previous_cpu_ns_.find(proc.pid);
    if (have_total_delta && prev != previous_cpu_ns_.end()
        && proc.cpu_time_ns > prev->second) {
      const std::uint64_t delta = proc.cpu_time_ns - prev->second;
      proc.cpu_percent = static_cast<double>(delta) * 100.0 /
                         static_cast<double>(total_delta);
    } else {
      proc.cpu_percent = 0.0;
    }

    proc.mem_percent = system.mem_total > 0
                           ? static_cast<double>(proc.rss_bytes) * 100.0 /
                                 static_cast<double>(system.mem_total)
                           : 0.0;
  }

  previous_cpu_ns_ = std::move(next_cpu_ns);
  previous_total_cpu_ns_ = total_ns;
  has_previous_total_ = true;
}

}  // namespace tasks_lens
