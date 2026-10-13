#ifndef TASKS_LENS_SCRIPTS_FORMAT_H
#define TASKS_LENS_SCRIPTS_FORMAT_H

#include <cstdint>
#include <cstdio>
#include <string>

#include "Data/process_info.h"

namespace tasks_lens {

// Left-aligned, truncated to width: every cell of a column is the same size, so
// the columns stay aligned. Same helper the scene builder used.
inline std::string pad(const std::string& text, int width) {
  if (width <= 0) return {};
  if (static_cast<int>(text.size()) >= width) return text.substr(0, width);
  return text + std::string(width - text.size(), ' ');
}

// 12.4M-style resident memory: one decimal and a unit, base 1024.
inline std::string format_bytes(std::uint64_t bytes) {
  char buffer[32];
  const double value = static_cast<double>(bytes);
  if (bytes >= 1024ull * 1024 * 1024) {
    std::snprintf(buffer, sizeof(buffer), "%.1fG", value / (1024.0 * 1024 * 1024));
  } else if (bytes >= 1024ull * 1024) {
    std::snprintf(buffer, sizeof(buffer), "%.1fM", value / (1024.0 * 1024));
  } else if (bytes >= 1024ull) {
    std::snprintf(buffer, sizeof(buffer), "%.0fK", value / 1024.0);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%lluB",
                  static_cast<unsigned long long>(bytes));
  }
  return buffer;
}

// Accumulated CPU time as M:SS or H:MM:SS once it passes an hour.
inline std::string format_cpu_time(std::uint64_t ns) {
  const std::uint64_t total_seconds = ns / 1000000000ull;
  const std::uint64_t hours = total_seconds / 3600;
  const std::uint64_t minutes = (total_seconds / 60) % 60;
  const std::uint64_t seconds = total_seconds % 60;

  char buffer[32];
  if (hours > 0) {
    std::snprintf(buffer, sizeof(buffer), "%llu:%02llu:%02llu",
                  static_cast<unsigned long long>(hours),
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu:%02llu",
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds));
  }
  return buffer;
}

inline std::string format_percent(double value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_FORMAT_H
