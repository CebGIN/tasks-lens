#ifndef TASKS_LENS_SCRIPTS_PRIORITY_INPUT_H
#define TASKS_LENS_SCRIPTS_PRIORITY_INPUT_H

#include <algorithm>
#include <exception>
#include <optional>
#include <string>

namespace tasks_lens {

// `text` parsed as a whole int and clamped to the nice range [-20, 19]. Returns
// nullopt when `text` is not a complete integer.
inline std::optional<int> parse_priority(const std::string& text) {
  int value = 0;
  try {
    std::size_t used = 0;
    value = std::stoi(text, &used);
    if (used != text.size()) return std::nullopt;
  } catch (const std::exception&) {
    return std::nullopt;
  }
  return std::clamp(value, -20, 19);
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_PRIORITY_INPUT_H
