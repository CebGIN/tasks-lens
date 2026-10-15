#ifndef TASKS_LENS_DATA_PROCESS_TREE_H
#define TASKS_LENS_DATA_PROCESS_TREE_H

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Data/process_info.h"

namespace tasks_lens {

namespace detail {

// Appends `pid` and its subtree to `lines`, two spaces per depth level.
inline void append_subtree(
    int pid, int depth, const std::unordered_map<int, std::vector<int>>& children,
    const std::unordered_map<int, const ProcessInfo*>& by_pid,
    std::unordered_set<int>& visited, std::vector<std::string>& lines) {
  if (depth > 64 || visited.count(pid) != 0) return;
  visited.insert(pid);

  const auto it = by_pid.find(pid);
  std::string text(static_cast<std::size_t>(depth) * 2, ' ');
  if (depth > 0) text += "\u2514 ";
  text += std::to_string(pid) + "  " +
          (it != by_pid.end() ? it->second->command : std::string());
  lines.push_back(std::move(text));

  const auto cit = children.find(pid);
  if (cit != children.end()) {
    for (int child : cit->second) {
      append_subtree(child, depth + 1, children, by_pid, visited, lines);
    }
  }
}

}  // namespace detail

// The snapshot as an indented parent -> child list. A process whose parent is
// not in `processes` is a root; processes the roots did not reach (a cycle, or a
// vanished parent) are appended at depth zero so nothing is dropped.
inline std::vector<std::string> build_process_tree_lines(
    const std::vector<ProcessInfo>& processes) {
  std::unordered_map<int, const ProcessInfo*> by_pid;
  std::unordered_map<int, std::vector<int>> children;
  for (const ProcessInfo& proc : processes) by_pid[proc.pid] = &proc;
  for (const ProcessInfo& proc : processes) {
    if (proc.ppid != proc.pid) children[proc.ppid].push_back(proc.pid);
  }
  for (auto& entry : children) std::sort(entry.second.begin(), entry.second.end());

  std::vector<std::string> lines;
  lines.reserve(processes.size());
  std::unordered_set<int> visited;
  for (const ProcessInfo& proc : processes) {
    if (by_pid.find(proc.ppid) == by_pid.end()) {
      detail::append_subtree(proc.pid, 0, children, by_pid, visited, lines);
    }
  }
  for (const ProcessInfo& proc : processes) {
    detail::append_subtree(proc.pid, 0, children, by_pid, visited, lines);
  }
  return lines;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_DATA_PROCESS_TREE_H
