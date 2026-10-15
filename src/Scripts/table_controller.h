#ifndef TASKS_LENS_SCRIPTS_TABLE_CONTROLLER_H
#define TASKS_LENS_SCRIPTS_TABLE_CONTROLLER_H

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "Data/process_info.h"
#include "Scripts/marquee.h"
#include "Scripts/process_view.h"
#include "Scripts/table_layout.h"
#include "Scripts/text_util.h"
#include "Scripts/viewport.h"

namespace tasks_lens {

// Header band style for every state: the Normal background/text reflect whether
// the column is the active sort key; Hover/Pressed/Disabled are fixed.
inline void apply_header_style(guayaba::Button* btn, bool is_active) {
  if (btn == nullptr) return;

  // No horizontal margin: the button stretches across its column, so padding
  // would only inflate the column's minimum. The vertical margin keeps it tall.
  guayaba::StyleBox normal;
  normal.set_content_margin(0, 1, 0, 1);
  normal.set_background_color(is_active ? guayaba::Color::BRIGHT_BLUE
                                        : guayaba::Color::BLUE);
  btn->set_stylebox(guayaba::ButtonState::Normal, normal);
  btn->set_text_color(guayaba::ButtonState::Normal,
                      is_active ? guayaba::Color::BRIGHT_GREEN
                                : guayaba::Color::CYAN);

  guayaba::StyleBox hover;
  hover.set_content_margin(0, 1, 0, 1);
  hover.set_background_color(guayaba::Color::BRIGHT_BLUE);
  btn->set_stylebox(guayaba::ButtonState::Hover, hover);
  btn->set_text_color(guayaba::ButtonState::Hover, guayaba::Color::BRIGHT_YELLOW);

  guayaba::StyleBox pressed;
  pressed.set_content_margin(0, 1, 0, 1);
  pressed.set_background_color(guayaba::Color::BRIGHT_WHITE);
  btn->set_stylebox(guayaba::ButtonState::Pressed, pressed);
  btn->set_text_color(guayaba::ButtonState::Pressed, guayaba::Color::BLACK);

  guayaba::StyleBox disabled;
  disabled.set_content_margin(0, 1, 0, 1);
  disabled.set_background_color(guayaba::Color::BLACK);
  btn->set_stylebox(guayaba::ButtonState::Disabled, disabled);
  btn->set_text_color(guayaba::ButtonState::Disabled, guayaba::Color::BLACK);
}

/*
    Owns the process table's presentation: the sort state, the header buttons,
    and a window of row labels sized to the table's height. Rows are virtualized
    (only capacity() labels exist per column) and render() writes the slice
    [offset, offset+capacity) into them.

    Attached to the table PanelContainer; the scene builder hands it the per
    column rows VBox and header button, and the Monitor script drives it.
*/
class TableController : public guayaba::Script<guayaba::PanelContainer> {
 public:
  // One column as handed in by the scene builder: identity plus the nodes the
  // controller fills and wires.
  struct ColumnSpec {
    std::string key;
    int width;
    int min_width;  // floor for the filling column
    bool fills;     // grows to use the columns the fixed ones leave
    guayaba::NodeRef<guayaba::VBox> rows;
    guayaba::NodeRef<guayaba::Button> header;
  };

  explicit TableController(std::vector<ColumnSpec> columns)
      : columns_(std::move(columns)), pools_(columns_.size()),
        last_text_(columns_.size()) {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].fills) flex_index_ = static_cast<int>(i);
      if (columns_[i].key == "command") marquee_index_ = static_cast<int>(i);
    }
  }

  const std::string& sort_key() const { return sort_key_; }
  bool ascending() const { return ascending_; }

  // Next column becomes active, descending; re-selecting the active column
  // toggles direction.
  void cycle_sort() {
    if (columns_.empty()) return;
    std::size_t index = 0;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].key == sort_key_) {
        index = i;
        break;
      }
    }
    const std::string& next = columns_[(index + 1) % columns_.size()].key;
    if (next == sort_key_) {
      ascending_ = !ascending_;
    } else {
      sort_key_ = next;
      ascending_ = false;
    }
    refresh_headers();
  }

  int capacity() const { return capacity_; }

  // --- Selection ------------------------------------------------------------

  // Index of the highlighted row in the current on-screen order, or -1 when
  // nothing is selected (or the table is empty). Resolved by render().
  int selected_index() const { return selected_index_; }

  // Moves the highlight by `delta` rows in the order `procs` is in. The cursor
  // is remembered by PID, so a re-sort between frames cannot slide the highlight
  // onto a different process.
  void select_by(int delta, const std::vector<ProcessInfo>& procs) {
    if (procs.empty()) return;
    int index = index_of_pid(procs, selected_pid_);
    if (index < 0) index = 0;  // nothing selected yet: start at the top
    index = std::max(0, std::min(index + delta, static_cast<int>(procs.size()) - 1));
    selected_pid_ = procs[static_cast<std::size_t>(index)].pid;
  }

  // Absolute selection, clamped; a caller can pass a large number for "last row".
  void select_to(int index, const std::vector<ProcessInfo>& procs) {
    if (procs.empty()) return;
    index = std::max(0, std::min(index, static_cast<int>(procs.size()) - 1));
    selected_pid_ = procs[static_cast<std::size_t>(index)].pid;
  }

  // Selects the row under a click, hit-tested against the first column's rows
  // box (all columns share the same rows) using last frame's resolved rect.
  bool select_at(guayaba::Vec2 point, const std::vector<ProcessInfo>& procs) {
    if (columns_.empty() || procs.empty()) return false;
    guayaba::VBox* rows = columns_[0].rows.get();
    if (rows == nullptr) return false;
    const guayaba::Rect rect = rows->get_rect();
    if (!rect.contains(point)) return false;
    const int index = offset_ + (point.y - rect.top());
    if (index < 0 || index >= static_cast<int>(procs.size())) return false;
    selected_pid_ = procs[static_cast<std::size_t>(index)].pid;
    return true;
  }

  // Writes the current window of `procs` into the row labels. Call once per
  // frame after sorting, so sort and scroll feel instant. `delta_time` drives the
  // selected row's command marquee.
  void render(const std::vector<ProcessInfo>& procs, double delta_time) {
    ensure_capacity();

    const int max_offset = std::max(0, static_cast<int>(procs.size()) - capacity_);
    offset_ = std::max(0, std::min(offset_, max_offset));

    // Resolve the cursor against this frame's order; a selection that no longer
    // exists falls back to the top row so an action always has a target.
    selected_index_ = index_of_pid(procs, selected_pid_);
    if (selected_index_ < 0 && !procs.empty()) {
      selected_index_ = 0;
      selected_pid_ = procs.front().pid;
    }
    // Keep the highlighted row inside the window.
    if (selected_index_ >= 0) {
      if (selected_index_ < offset_) {
        offset_ = selected_index_;
      } else if (selected_index_ >= offset_ + capacity_) {
        offset_ = selected_index_ - capacity_ + 1;
      }
      offset_ = std::max(0, std::min(offset_, max_offset));
    }

    // The selected row's command is the one cell that can be wider than its
    // column, so it scrolls through a marquee. Advanced once per frame here, not
    // per cell.
    if (marquee_index_ >= 0 && selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(procs.size())) {
      marquee_.set(procs[static_cast<std::size_t>(selected_index_)].command,
                   columns_[static_cast<std::size_t>(marquee_index_)].width);
      marquee_.advance(delta_time);
    } else {
      marquee_.set("", 0);
    }

    for (std::size_t c = 0; c < columns_.size(); ++c) {
      const ColumnSpec& column = columns_[c];
      for (int i = 0; i < capacity_; ++i) {
        const int index = offset_ + i;
        guayaba::Label* label = pools_[c][static_cast<std::size_t>(i)].get();
        if (label == nullptr) continue;

        std::string text;
        const char* color = guayaba::Color::WHITE;
        bool selected = false;
        if (index < static_cast<int>(procs.size())) {
          const ProcessInfo& proc = procs[static_cast<std::size_t>(index)];
          color = row_color(proc);
          selected = index == selected_index_;
          if (selected && static_cast<int>(c) == marquee_index_) {
            text = marquee_.visible();
          } else {
            text = cell_text(proc, column.key, column.width);
          }
        } else {
          text = pad("", column.width);
        }

        if (last_text_[c][static_cast<std::size_t>(i)] != text) {
          label->set_text(text);
          last_text_[c][static_cast<std::size_t>(i)] = text;
        }
        label->set_text_color(color);
        // The cursor band is a background, so the per-state text colour stays
        // readable on it.
        label->set_background_color(selected ? "BRIGHT_BLACK" : "BLACK");
      }
    }
  }

  void on_attached() override {
    for (const ColumnSpec& column : columns_) {
      guayaba::Button* btn = column.header.get();
      if (btn == nullptr) continue;
      connect_script_signal("clicked", btn, this, &TableController::on_header_clicked);
    }
    refresh_headers();
  }

 private:
  void on_header_clicked() {
    for (const ColumnSpec& column : columns_) {
      guayaba::Button* btn = column.header.get();
      if (btn != nullptr && btn->is_inside(guayaba::Input::mouse_pos)) {
        const std::string key = column.key;
        if (key == sort_key_) {
          ascending_ = !ascending_;
        } else {
          sort_key_ = key;
          ascending_ = false;
        }
        refresh_headers();
        return;
      }
    }
  }

  void refresh_headers() {
    for (const ColumnSpec& column : columns_) {
      apply_header_style(column.header.get(), column.key == sort_key_);
    }
  }

  // Recomputes capacity and the filling column's width from the tty size, and
  // grows or shrinks each column's label pool to match. Pools only change on a
  // resize.
  void ensure_capacity() {
    if (columns_.empty()) return;

    int header_height = 3;
    if (columns_[0].header.get() != nullptr) {
      header_height = columns_[0].header->get_rect().size.y;
    }

    std::vector<int> widths;
    std::vector<int> min_widths;
    widths.reserve(columns_.size());
    min_widths.reserve(columns_.size());
    for (const ColumnSpec& column : columns_) {
      widths.push_back(column.width);
      min_widths.push_back(column.min_width);
    }

    // The row count comes from the tty, not from a resolved rect: reading the
    // pool's own rect back would make capacity a function of the previous
    // capacity, so the page could grow but never shrink.
    int screen_rows = terminal_rows();
    if (screen_rows <= 0) screen_rows = node->get_rect().size.y;

    // Everything between the screen edge and the table's rows: the outer panel
    // border plus every layout row above and below the table.
    int chrome = 2;
    if (guayaba::Control* parent =
            dynamic_cast<guayaba::Control*>(node->get_parent().get())) {
      chrome += parent->get_minimum_size().y - node->get_minimum_size().y;
    }

    const TableLayout layout =
        compute_table_layout(screen_rows, terminal_cols(), chrome, header_height,
                             widths, min_widths, flex_index_);
    if (layout.has_flex) columns_[flex_index_].width = layout.flex_width;

    const int capacity = layout.capacity;
    if (capacity == capacity_ && pools_[0].size() == static_cast<std::size_t>(capacity)) {
      return;
    }
    capacity_ = capacity;

    for (std::size_t c = 0; c < columns_.size(); ++c) {
      guayaba::VBox* rows = columns_[c].rows.get();
      if (rows == nullptr) continue;

      std::vector<guayaba::NodeRef<guayaba::Label>>& pool = pools_[c];
      std::vector<std::string>& last = last_text_[c];

      while (static_cast<int>(pool.size()) < capacity_) {
        const int index = static_cast<int>(pool.size());
        guayaba::NodeRef<guayaba::Label> label = rows->add_child<guayaba::Label>(
            "cell_" + columns_[c].key + "_" + std::to_string(index));
        label->set_fit_content(true);
        label->set_text_color(guayaba::Color::WHITE);
        pool.push_back(label);
        last.emplace_back();
      }
      while (static_cast<int>(pool.size()) > capacity_) {
        rows->remove_child(pool.size() - 1);
        pool.pop_back();
        last.pop_back();
      }
    }
  }

  // Index of `pid` in `procs`, or -1.
  int index_of_pid(const std::vector<ProcessInfo>& procs, int pid) const {
    for (std::size_t i = 0; i < procs.size(); ++i) {
      if (procs[i].pid == pid) return static_cast<int>(i);
    }
    return -1;
  }

  std::vector<ColumnSpec> columns_;
  std::vector<std::vector<guayaba::NodeRef<guayaba::Label>>> pools_;
  std::vector<std::vector<std::string>> last_text_;

  int capacity_ = 0;
  int offset_ = 0;
  int flex_index_ = -1;
  // Index of the COMMAND column, whose selected cell scrolls; -1 if none.
  int marquee_index_ = -1;
  Marquee marquee_;
  // The cursor, by PID (0 = none); selected_index_ is derived each render().
  int selected_pid_ = 0;
  int selected_index_ = -1;
  // PID ascending is the stable default; CPU% reshuffles several times a second.
  std::string sort_key_ = "pid";
  bool ascending_ = true;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_TABLE_CONTROLLER_H
