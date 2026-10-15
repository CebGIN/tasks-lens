#ifndef TASKS_LENS_SCRIPTS_PROCESS_VIEW_H
#define TASKS_LENS_SCRIPTS_PROCESS_VIEW_H

#include <string>

#include <guayaba/Guayaba.hpp>

#include "Data/process_info.h"
#include "Scripts/format.h"
#include "Scripts/text_util.h"

namespace tasks_lens {

// One table cell as text, padded to `width` for the column named `key`; an
// unknown key yields blank padding.
inline std::string cell_text(const ProcessInfo& proc, const std::string& key,
                             int width) {
  if (key == "pid") return pad(std::to_string(proc.pid), width);
  if (key == "ppid") return pad(std::to_string(proc.ppid), width);
  if (key == "user") return pad(proc.user, width);
  if (key == "state") return pad(process_state_code(proc.state), width);
  if (key == "cpu") return pad(format_percent(proc.cpu_percent), width);
  if (key == "mem") return pad(format_percent(proc.mem_percent), width);
  if (key == "res") return pad(format_bytes(proc.rss_bytes), width);
  if (key == "thr") return pad(std::to_string(proc.thread_count), width);
  if (key == "pri") return pad(std::to_string(proc.priority), width);
  if (key == "time") return pad(format_cpu_time(proc.cpu_time_ns), width);
  if (key == "command") return pad(proc.command, width);
  return pad("", width);
}

// Row text colour: bright red for a zombie, bright yellow for I/O-blocked,
// white otherwise.
inline const char* row_color(const ProcessInfo& proc) {
  if (proc.state == ProcessState::Zombie) return guayaba::Color::BRIGHT_RED;
  if (proc.state == ProcessState::DiskSleep) return guayaba::Color::BRIGHT_YELLOW;
  return guayaba::Color::WHITE;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_PROCESS_VIEW_H
