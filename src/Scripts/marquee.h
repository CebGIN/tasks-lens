#ifndef TASKS_LENS_SCRIPTS_MARQUEE_H
#define TASKS_LENS_SCRIPTS_MARQUEE_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Core/Utf8.hpp>

namespace tasks_lens {

/*
    A one-line marquee for a cell that can be wider than its column.

    When the text fits the window it is shown whole (padded to the window width).
    When it does not, the marquee steps a `width`-cell window across the text one
    cell at a time, wrapping around through a short gap so the join is visible and
    the whole command can be read.

    It is just the offset and the clock - the owner decides which cell it applies
    to and writes visible() into that label. That keeps the timing out of the
    table's per-cell loop, and makes the scroll a property of the selection rather
    than of any one label.

    Everything is measured in codepoints, matching the engine's grid, so a
    multi-byte glyph is one cell and is never split by the window.
*/
class Marquee {
 public:
  // Seconds per cell. Slow enough to read, fast enough that even a long command
  // cycles in a few seconds.
  static constexpr double kStepSeconds = 0.2;

  // Points the marquee at `text` shown in a `width`-cell window. A change of text
  // or width restarts at the first cell, so a new selection is read from its
  // start rather than from wherever the previous one had scrolled to.
  void set(const std::string& text, int width) {
    if (text == text_ && width == width_) return;
    text_ = text;
    width_ = width;
    offset_ = 0;
    elapsed_ = 0.0;
    looped_.clear();
    cells_.clear();
    scrolling_ = false;
    if (width_ <= 0) return;

    // The loop the window walks: the text plus a gap, so the wrap-around does not
    // glue the end of the command to its start.
    looped_ = text_ + "   ";
    cells_ = split_codepoints(looped_);
    scrolling_ = guayaba::text_width(text_) > width_;
  }

  // Advances the window one cell every kStepSeconds. A no-op while the text fits.
  void advance(double delta_time) {
    if (!scrolling_ || cells_.empty()) return;
    elapsed_ += delta_time;
    const int steps = static_cast<int>(elapsed_ / kStepSeconds);
    if (steps <= 0) return;
    elapsed_ -= steps * kStepSeconds;
    offset_ = (offset_ + steps) % static_cast<int>(cells_.size());
  }

  // The cells to show now: exactly `width` of them, so the label's minimum stays
  // the column width and the row does not reflow as it scrolls.
  std::string visible() const {
    if (width_ <= 0) return {};
    if (!scrolling_) return pad_cells(text_, width_);

    std::string out;
    const int count = static_cast<int>(cells_.size());
    for (int i = 0; i < width_; ++i) {
      const std::pair<std::size_t, std::size_t>& cell =
          cells_[static_cast<std::size_t>((offset_ + i) % count)];
      out.append(looped_, cell.first, cell.second);
    }
    return out;
  }

 private:
  // Byte offset and length of each codepoint, so the window can slice whole
  // glyphs. Built once per set().
  static std::vector<std::pair<std::size_t, std::size_t>> split_codepoints(
      const std::string& text) {
    std::vector<std::pair<std::size_t, std::size_t>> cells;
    std::size_t index = 0;
    while (index < text.size()) {
      char32_t codepoint = 0;
      const std::size_t length = guayaba::decode_utf8(text, index, codepoint);
      cells.emplace_back(index, length);
      index += length;
    }
    return cells;
  }

  // `text` clipped (or padded) to exactly `width` codepoints.
  static std::string pad_cells(const std::string& text, int width) {
    std::string out;
    int cells = 0;
    std::size_t index = 0;
    while (index < text.size() && cells < width) {
      char32_t codepoint = 0;
      const std::size_t length = guayaba::decode_utf8(text, index, codepoint);
      out.append(text, index, length);
      index += length;
      ++cells;
    }
    if (cells < width) out.append(static_cast<std::size_t>(width - cells), ' ');
    return out;
  }

  std::string text_;
  std::string looped_;
  std::vector<std::pair<std::size_t, std::size_t>> cells_;
  int width_ = 0;
  int offset_ = 0;
  double elapsed_ = 0.0;
  bool scrolling_ = false;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_MARQUEE_H
