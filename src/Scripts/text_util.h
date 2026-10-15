#ifndef TASKS_LENS_SCRIPTS_TEXT_UTIL_H
#define TASKS_LENS_SCRIPTS_TEXT_UTIL_H

#include <cstddef>
#include <string>

#include <guayaba/Core/Utf8.hpp>

namespace tasks_lens {

// `text` truncated to `width` bytes, or space-padded to exactly `width`.
inline std::string pad(const std::string& text, int width) {
  if (width <= 0) return {};
  if (static_cast<int>(text.size()) >= width) return text.substr(0, width);
  return text + std::string(width - text.size(), ' ');
}

// The first `max` bytes of `text`, plus an ellipsis when it had to cut.
inline std::string truncate(const std::string& text, std::size_t max) {
  if (text.size() <= max) return text;
  return text.substr(0, max) + "\u2026";
}

// The first `max_cells` codepoints of `text`; a multi-byte glyph is never cut.
inline std::string clip_cells(const std::string& text, int max_cells) {
  if (max_cells <= 0) return {};
  std::string out;
  int cells = 0;
  std::size_t index = 0;
  while (index < text.size() && cells < max_cells) {
    char32_t codepoint = 0;
    const std::size_t length = guayaba::decode_utf8(text, index, codepoint);
    out.append(text, index, length);
    index += length;
    ++cells;
  }
  return out;
}

// `text` clipped or space-padded to exactly `width` codepoints.
inline std::string pad_cells(const std::string& text, int width) {
  if (width <= 0) return {};
  std::string out = clip_cells(text, width);
  int cells = 0;
  std::size_t index = 0;
  while (index < out.size()) {
    char32_t codepoint = 0;
    index += guayaba::decode_utf8(out, index, codepoint);
    ++cells;
  }
  if (cells < width) out.append(static_cast<std::size_t>(width - cells), ' ');
  return out;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_TEXT_UTIL_H
