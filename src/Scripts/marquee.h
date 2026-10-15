#ifndef TASKS_LENS_SCRIPTS_MARQUEE_H
#define TASKS_LENS_SCRIPTS_MARQUEE_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Core/Utf8.hpp>

#include "Scripts/text_util.h"

namespace tasks_lens {

/*
    A one-line marquee for a cell that can be wider than its column. When the
    text fits the window it is shown whole padded to the window width; when it
    does not, a `width`-cell window steps across the text one cell at a time,
    wrapping through a short gap. Everything is measured in codepoints, so a
    multi-byte glyph is one cell and is never split.
*/
class Marquee {
 public:
  // Seconds per cell.
  static constexpr double kStepSeconds = 0.2;

  // Points the marquee at `text` shown in a `width`-cell window. A change of text
  // or width restarts at the first cell.
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

    // The loop the window walks: the text plus a gap so the wrap-around is
    // visible.
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

  // The cells to show now: exactly `width` of them.
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
  // glyphs.
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
