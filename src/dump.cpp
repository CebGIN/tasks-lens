#include "dump.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "Data/cpu_sampler.h"
#include "Data/process_source.h"

// Takes two snapshots a second apart so the CPU deltas are non-zero, then prints
// a top-like excerpt to stdout. No terminal takeover, so it can run in a pipe.
int run_dump() {
  std::unique_ptr<tasks_lens::ProcessSource> source =
      tasks_lens::create_system_process_source();
  tasks_lens::CpuSampler sampler;

  for (int pass = 0; pass < 2; ++pass) {
    tasks_lens::SystemInfo sys = source->read_system();
    std::vector<tasks_lens::ProcessInfo> procs = source->read_processes();
    sampler.apply(sys, procs);

    std::printf("=== %s pass %d ===\n", source->platform_name(), pass + 1);
    std::printf(
        "tasks %d total, %d running, %d stopped, %d zombie | load %.2f %.2f %.2f\n",
        sys.total, sys.running, sys.stopped, sys.zombie, sys.load_avg[0],
        sys.load_avg[1], sys.load_avg[2]);
    std::printf("mem %.1f MiB total, %.1f MiB used, %.1f MiB avail | cpu %.1fs\n",
                sys.mem_total / 1048576.0, sys.mem_used / 1048576.0,
                sys.mem_available / 1048576.0, sys.total_cpu_time_ns / 1e9);

    std::sort(procs.begin(), procs.end(),
              [](const tasks_lens::ProcessInfo& a,
                 const tasks_lens::ProcessInfo& b) {
                return a.cpu_percent > b.cpu_percent;
              });

    const std::size_t shown = procs.size() < 10 ? procs.size() : 10;
    for (std::size_t i = 0; i < shown; ++i) {
      const tasks_lens::ProcessInfo& p = procs[i];
      std::printf("%7d %7d %-8s %s %5.1f %5.1f %8.1fM t%-3d n%-3d %8.2fs  %s\n",
                  p.pid, p.ppid, p.user.c_str(),
                  tasks_lens::process_state_code(p.state), p.cpu_percent,
                  p.mem_percent, p.rss_bytes / 1048576.0, p.thread_count,
                  p.priority, p.cpu_time_ns / 1e9, p.command.c_str());
    }
    if (pass == 0) std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}
