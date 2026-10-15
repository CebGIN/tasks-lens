#ifndef TASKS_LENS_DATA_HISTORY_STORE_H
#define TASKS_LENS_DATA_HISTORY_STORE_H

#include <cstddef>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Data/process_info.h"

namespace tasks_lens {

// One capped CPU% series per pid, for the history view. Keyed by pid so a series
// survives a re-sort; series of processes that exited are pruned on each record.
class HistoryStore {
 public:
  static constexpr std::size_t kSamples = 120;

  // Appends each process's cpu_percent to its series and drops the series of
  // every process not in `processes`.
  void record(const std::vector<ProcessInfo>& processes) {
    std::unordered_set<int> alive;
    alive.reserve(processes.size());
    for (const ProcessInfo& proc : processes) {
      alive.insert(proc.pid);
      std::deque<double>& series = series_[proc.pid];
      series.push_back(proc.cpu_percent);
      while (series.size() > kSamples) series.pop_front();
    }
    for (auto it = series_.begin(); it != series_.end();) {
      if (alive.count(it->first) == 0) {
        it = series_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // The series for `pid`, or nullptr when there is none.
  const std::deque<double>* series(int pid) const {
    const auto it = series_.find(pid);
    return it != series_.end() ? &it->second : nullptr;
  }

 private:
  std::unordered_map<int, std::deque<double>> series_;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_HISTORY_STORE_H
