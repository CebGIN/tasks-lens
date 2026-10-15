#ifndef TASKS_LENS_SCRIPTS_TABLE_CONTROLLER_H
#define TASKS_LENS_SCRIPTS_TABLE_CONTROLLER_H

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>
#include <guayaba/Terminal/IOSTerminal.hpp>

#include "Data/process_info.h"
#include "Scripts/format.h"
#include "Scripts/marquee.h"

namespace tasks_lens {

// Header band style shared by the scene builder and the controller: one source
// of truth for how a column header looks in its Normal state, active or not.
inline void apply_header_normal_style(guayaba::Button* btn, bool is_active) {
  guayaba::StyleBox normal;
  // No horizontal margin: the button stretches across its column (anchored to
  // both edges), so padding would only inflate the column's minimum past the
  // width the column was given. The vertical margin keeps the header band tall.
  normal.set_content_margin(0, 1, 0, 1);
  normal.set_background_color(is_active ? guayaba::Color::BRIGHT_BLUE
                                        : guayaba::Color::BLUE);
  btn->set_stylebox(guayaba::ButtonState::Normal, normal);
  btn->set_text_color(guayaba::ButtonState::Normal,
                      is_active ? guayaba::Color::BRIGHT_GREEN
                                : guayaba::Color::CYAN);
}

// One cell of the process table as text, padded to the column width.
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

// Zombies and I/O-blocked processes are the two the spec singles out.
inline const char* row_color(const ProcessInfo& proc) {
  if (proc.state == ProcessState::Zombie) return guayaba::Color::BRIGHT_RED;
  if (proc.state == ProcessState::DiskSleep) return guayaba::Color::BRIGHT_YELLOW;
  return guayaba::Color::WHITE;
}

// Less-than for the sort keys, so the active column and direction are one place.
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

/*
    Owns the process table's presentation: the sort state, the header buttons,
    and a window of row labels sized to the table's height.

    Rows are virtualized: only capacity() labels exist per column, and render()
    writes the slice [offset, offset+capacity) into them. That keeps a machine
    with hundreds of processes from building hundreds of nodes.

    Attached to the table PanelContainer. The scene builder hands it the per
    column rows VBox and header button; the Monitor script drives it.
*/
class TableController : public guayaba::Script<guayaba::PanelContainer> {
 public:
  struct Column {
    std::string key;
    int width;
    bool fills;     // grows to fill the columns the fixed ones leave
    int min_width;  // floor for a filling column
    guayaba::NodeRef<guayaba::VBox> rows;
    guayaba::NodeRef<guayaba::Button> header;
  };

  explicit TableController(std::vector<Column> columns)
      : columns_(std::move(columns)), pools_(columns_.size()),
        last_text_(columns_.size()),
        terminal_(guayaba::create_system_terminal()) {
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].fills) flex_index_ = static_cast<int>(i);
      if (columns_[i].key == "command") marquee_index_ = static_cast<int>(i);
    }
  }

  const std::string& sort_key() const { return sort_key_; }
  bool ascending() const { return ascending_; }

  // Next column becomes active, descending; re-selecting the active column
  // toggles direction.
  void cycle_sort() {
    size_t index = 0;
    for (size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].key == sort_key_) {
        index = i;
        break;
      }
    }
    if (columns_.empty()) return;
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

  // The terminal's column count, or 0 when it cannot be read. The Monitor uses
  // it to keep the status/summary lines from widening the layout: a fit_content
  // label wider than the screen grows the whole layout VBox, which pushes the
  // toolbar's right-aligned buttons off the edge.
  int screen_cols() const {
    return terminal_ ? terminal_->get_screen_size({0, 0}).x : 0;
  }

  // --- Selection ------------------------------------------------------------

  // Index of the highlighted row in the current on-screen order, or -1 when
  // nothing is selected (or the table is empty). Resolved by render().
  int selected_index() const { return selected_index_; }

  // PID of the highlighted process, or 0 when none. This is what actions and
  // the priority editor will be pointed at.
  int selected_pid() const { return selected_pid_; }

  // Moves the highlight by `delta` rows in the order `procs` is in. The cursor
  // is remembered by PID, not by index, so a re-sort between frames (which is
  // exactly what happens when sorting by %CPU) cannot slide the highlight onto
  // a different process.
  void select_by(int delta, const std::vector<ProcessInfo>& procs) {
    if (procs.empty()) return;
    int index = index_of_pid(procs, selected_pid_);
    if (index < 0) index = 0;  // nothing selected yet: start at the top
    index = std::max(0, std::min(index + delta, static_cast<int>(procs.size()) - 1));
    selected_pid_ = procs[static_cast<std::size_t>(index)].pid;
  }

  // Absolute selection, clamped; a caller can pass a large number to mean
  // "the last row".
  void select_to(int index, const std::vector<ProcessInfo>& procs) {
    if (procs.empty()) return;
    index = std::max(0, std::min(index, static_cast<int>(procs.size()) - 1));
    selected_pid_ = procs[static_cast<std::size_t>(index)].pid;
  }

  // Selects the row under a click. Hit-tests against the first column's rows
  // box (all columns share the same rows), using last frame's resolved rects,
  // which is the same trade ScrollView makes.
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

    // Resolve the cursor against this frame's order. A selection that no longer
    // exists (process gone, or the very first frame) falls back to the top row,
    // so there is always a target for an action.
    selected_index_ = index_of_pid(procs, selected_pid_);
    if (selected_index_ < 0 && !procs.empty()) {
      selected_index_ = 0;
      selected_pid_ = procs.front().pid;
    }
    // Keep the highlighted row inside the window; the cursor is the scroll
    // anchor, so it never scrolls off screen.
    if (selected_index_ >= 0) {
      if (selected_index_ < offset_) {
        offset_ = selected_index_;
      } else if (selected_index_ >= offset_ + capacity_) {
        offset_ = selected_index_ - capacity_ + 1;
      }
      offset_ = std::max(0, std::min(offset_, max_offset));
    }

    // The selected row's command is the one cell that can be wider than its
    // column, so it is shown as a marquee that cycles the full text through the
    // window. Advanced once per frame here, not per cell, so the clock is a
    // property of the selection rather than of any one label.
    if (marquee_index_ >= 0 && selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(procs.size())) {
      marquee_.set(procs[static_cast<std::size_t>(selected_index_)].command,
                   columns_[static_cast<std::size_t>(marquee_index_)].width);
      marquee_.advance(delta_time);
    } else {
      marquee_.set("", 0);
    }

    for (size_t c = 0; c < columns_.size(); ++c) {
      const Column& column = columns_[c];
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
        // The cursor band is a background, so the per-state text colour (zombie
        // red, I/O-blocked yellow) stays readable on it.
        label->set_background_color(selected ? "BRIGHT_BLACK" : "BLACK");
      }
    }
  }

  void on_attached() override {
    for (const Column& column : columns_) {
      guayaba::Button* btn = column.header.get();
      if (btn == nullptr) continue;
      connect_script_signal("clicked", btn, this, &TableController::on_header_clicked);
    }
    refresh_headers();
  }

 private:
  void on_header_clicked() {
    for (const Column& column : columns_) {
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
    for (const Column& column : columns_) {
      guayaba::Button* btn = column.header.get();
      if (btn == nullptr) continue;
      apply_header_normal_style(btn, column.key == sort_key_);
    }
  }

  // Recomputes how many rows fit from the table's resolved rect and grows or
  // shrinks each column's label pool to match. Pools only change on a resize.
  void ensure_capacity() {
    if (columns_.empty()) return;

    int header_height = 3;
    if (columns_[0].header.get() != nullptr) {
      header_height = columns_[0].header->get_rect().size.y;
    }

    // The filling column takes whatever the fixed ones leave, so a long COMMAND
    // is not clipped at its minimum while the rest of the row sits empty. Screen
    // width minus both double borders (outer panel + table) and the gaps between
    // columns is what the columns share; the fixed widths come off that and the
    // remainder is the filling column's cell. Its minimum floors the result on a
    // narrow terminal, where the table overflows and is clipped rather than
    // shoving the later columns past the right edge. The layout agrees: the
    // column's custom_minimum_size is that same floor and it is the one column
    // with grow_x, so the width worked out here is the width it is given.
    if (flex_index_ >= 0 && terminal_) {
      const int screen_cols = terminal_->get_screen_size({0, 0}).x;
      if (screen_cols > 0) {
        int fixed = 0;
        for (size_t i = 0; i < columns_.size(); ++i) {
          if (static_cast<int>(i) != flex_index_) fixed += columns_[i].width;
        }
        const int gaps = static_cast<int>(columns_.size()) - 1;
        const int width = std::max(columns_[flex_index_].min_width,
                                   screen_cols - 4 - gaps - fixed);
        columns_[flex_index_].width = width;
      }
    }

    // How tall the table is this frame, measured from the terminal, not from any
    // resolved rect. Every row label is a line in its column's minimum size, so
    // the table - and the panel around it, which is sized by its child - resolve
    // to at least the pool's height. Reading those rects back makes capacity a
    // function of the previous capacity: the page can grow but never shrink.
    // The tty size is the one value in the chain the pool does not inflate.
    int screen_rows = terminal_ ? terminal_->get_screen_size({0, 0}).y : 0;
    if (screen_rows <= 0) screen_rows = node->get_rect().size.y;

    // Everything between the screen edge and the table's own rows: the outer
    // panel border, plus every layout row above and below the table and their
    // gaps. The parent's minimum minus the table's isolates exactly those rows,
    // so it stays correct however many there are and whatever they wrap to.
    int chrome = 2;  // outer panel border
    if (guayaba::Control* parent =
            dynamic_cast<guayaba::Control*>(node->get_parent().get())) {
      chrome += parent->get_minimum_size().y - node->get_minimum_size().y;
    }
    const int table_height = screen_rows - chrome;

    // The table's own box: subtract its double border (2), the header row, and
    // the one-cell gap between header and rows.
    int rows_height = table_height - 2 - header_height - 1;
    int capacity = std::max(1, rows_height);
    if (capacity == capacity_ && pools_[0].size() == static_cast<size_t>(capacity)) {
      return;
    }
    capacity_ = capacity;

    for (size_t c = 0; c < columns_.size(); ++c) {
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

  // Index of `pid` in `procs`, or -1. The cursor is stored by PID rather than
  // by position because the position is meaningless after a re-sort.
  int index_of_pid(const std::vector<ProcessInfo>& procs, int pid) const {
    for (std::size_t i = 0; i < procs.size(); ++i) {
      if (procs[i].pid == pid) return static_cast<int>(i);
    }
    return -1;
  }

  std::vector<Column> columns_;
  std::vector<std::vector<guayaba::NodeRef<guayaba::Label>>> pools_;
  std::vector<std::vector<std::string>> last_text_;
  // The engine asks the tty for its size each frame; this is only to have that
  // one true measurement the layout rects cannot give us. Never init()ed, so it
  // does not touch raw mode.
  std::unique_ptr<guayaba::IOSTerminal> terminal_;

  int capacity_ = 0;
  int offset_ = 0;
  int flex_index_ = -1;
  // Index of the COMMAND column, whose selected cell scrolls; -1 if there is
  // none. Set once from the column list.
  int marquee_index_ = -1;
  // The scroll state for that one cell.
  Marquee marquee_;
  // The cursor. 0 means "nothing"; PID is unique, so it survives a re-sort.
  // selected_index_ is derived from it every frame in render().
  int selected_pid_ = 0;
  int selected_index_ = -1;
  // PID ascending: a stable order. CPU% (the obvious "top" default) reshuffles
  // several times a second because the deltas swing, which reads as flicker.
  std::string sort_key_ = "pid";
  bool ascending_ = true;
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_TABLE_CONTROLLER_H
