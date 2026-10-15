#ifndef TASKS_LENS_SCRIPTS_PROCESS_SORT_H
#define TASKS_LENS_SCRIPTS_PROCESS_SORT_H

#include <string>

#include "Data/process_info.h"

namespace tasks_lens {

// Strict-less for `a` before `b` under the sort column `key`; an unknown key
// orders by cpu_percent.
inline bool process_less(const ProcessInfo& a, const ProcessInfo& b,
                         const std::string& key) {
  if (key == "pid") return a.pid < b.pid;
  if (key == "ppid") return a.ppid < b.ppid;
  if (key == "user") return a.user < b.user;
  if (key == "state") return a.state < b.state;
  if (key == "cpu") return a.cpu_percent < b.cpu_percent;
  if (key == "mem") return a.mem_percent < b.mem_percent;
  if (key == "res") return a.rss_bytes < b.rss_bytes;
  if (key == "thr") return a.thread_count < b.thread_count;
  if (key == "pri") return a.priority < b.priority;
  if (key == "time") return a.cpu_time_ns < b.cpu_time_ns;
  if (key == "command") return a.command < b.command;
  return a.cpu_percent < b.cpu_percent;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_PROCESS_SORT_H
