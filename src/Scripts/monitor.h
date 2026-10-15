#ifndef TASKS_LENS_SCRIPTS_MONITOR_H
#define TASKS_LENS_SCRIPTS_MONITOR_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <guayaba/Core/Utf8.hpp>
#include <guayaba/Guayaba.hpp>

#include "Data/cpu_sampler.h"
#include "Data/process_info.h"
#include "Data/process_source.h"
#include "Scripts/format.h"
#include "Scripts/table_controller.h"
#include "Scripts/ui_style.h"

namespace tasks_lens {

// The action buttons the Monitor wires and toggles. Grouped so the scene builder
// hands them over as one value instead of a dozen constructor arguments.
struct MonitorToolbar {
  guayaba::NodeRef<guayaba::Button> sort;
  guayaba::NodeRef<guayaba::Button> interval;
  guayaba::NodeRef<guayaba::Button> quit;
  guayaba::NodeRef<guayaba::Button> clear;
  guayaba::NodeRef<guayaba::Button> kill;
  guayaba::NodeRef<guayaba::Button> force;
  guayaba::NodeRef<guayaba::Button> stop;
  guayaba::NodeRef<guayaba::Button> cont;
  guayaba::NodeRef<guayaba::Button> nice;
  guayaba::NodeRef<guayaba::Button> threads;
  guayaba::NodeRef<guayaba::Button> tree;
  guayaba::NodeRef<guayaba::Button> history;
};

/*
    The monitor's brain, attached to the root node.

    Owns the data (the process source, the CPU sampler, the last snapshot) and
    the hotkeys. Every frame it sorts the snapshot by the table's current sort
    and hands it to the TableController, so sorting and scrolling feel instant;
    every refresh interval it re-reads the operating system, so the numbers move
    at the configured rate rather than at the frame rate.

    It is a Script<Node> on the root, so on_input runs after every control in the
    tree has had its turn: whatever a control consumed never reaches these
    hotkeys, which is what lets a future focused field eat keys without the
    global shortcuts firing.
*/
class Monitor : public guayaba::Script<guayaba::Node> {
  // What the bottom input line is currently for. None means the field is idle
  // and the hotkeys are live; the other two focus it and gate the hotkeys.
  enum class InputMode { None, Filter, Priority };

  // Which full-screen detail view is open, if any.
  enum class ViewKind { None, Threads, Tree, History };

 public:
  Monitor(std::unique_ptr<ProcessSource> source,
          guayaba::NodeRef<guayaba::Label> summary_tasks,
          guayaba::NodeRef<guayaba::Label> summary_mem,
          guayaba::NodeRef<guayaba::Label> selection,
          guayaba::NodeRef<guayaba::Label> status,
          guayaba::NodeRef<guayaba::LineEdit> prompt,
          MonitorToolbar toolbar,
          guayaba::NodeRef<guayaba::PanelContainer> table,
          TableController* table_controller)
      : source_(std::move(source)),
        summary_tasks_(summary_tasks),
        summary_mem_(summary_mem),
        selection_(selection),
        status_(status),
        prompt_(prompt),
        toolbar_(toolbar),
        table_(table),
        table_controller_(table_controller) {}

  // Wires the input field and every toolbar button. Done here rather than in the
  // scene builder because the Monitor is the one that knows what each one means.
  void on_attached() override {
    if (prompt_.get() != nullptr) {
      connect_script_signal("text_changed", prompt_.get(), this, &Monitor::on_prompt_changed);
      connect_script_signal("text_submitted", prompt_.get(), this, &Monitor::on_prompt_submitted);
    }

    connect_clicked(toolbar_.sort, &Monitor::on_button_sort);
    connect_clicked(toolbar_.interval, &Monitor::on_button_interval);
    connect_clicked(toolbar_.quit, &Monitor::on_button_quit);
    connect_clicked(toolbar_.clear, &Monitor::on_button_clear);
    connect_clicked(toolbar_.kill, &Monitor::on_button_kill);
    connect_clicked(toolbar_.force, &Monitor::on_button_force);
    connect_clicked(toolbar_.stop, &Monitor::on_button_stop);
    connect_clicked(toolbar_.cont, &Monitor::on_button_cont);
    connect_clicked(toolbar_.nice, &Monitor::on_button_nice);
    connect_clicked(toolbar_.threads, &Monitor::on_button_threads);
    connect_clicked(toolbar_.tree, &Monitor::on_button_tree);
    connect_clicked(toolbar_.history, &Monitor::on_button_history);

    update_toolbar();
  }

  void on_process(double delta_time) override {
    elapsed_ += delta_time;

    if (first_frame_ || elapsed_ >= interval_) {
      elapsed_ = 0.0;
      first_frame_ = false;
      refresh();
      dirty_ = true;
    }

    // The visible list is rebuilt only when something that changes it did: a new
    // snapshot, a filter edit, or a sort change. Rendering it every frame would
    // otherwise copy and re-sort the whole table 30 times a second.
    if (table_controller_ != nullptr &&
        (table_controller_->sort_key() != last_sort_key_ ||
         table_controller_->ascending() != last_ascending_)) {
      dirty_ = true;
    }
    if (dirty_) {
      rebuild_visible();
      if (table_controller_ != nullptr) {
        last_sort_key_ = table_controller_->sort_key();
        last_ascending_ = table_controller_->ascending();
      }
      dirty_ = false;
    }

    // Follows the search bar's focus so a click into it starts filtering.
    sync_search_focus();

    if (table_controller_ != nullptr) table_controller_->render(visible_, delta_time);
    update_selection();
    // Button enablement and the live Sort/Interval labels track the selection and
    // the sort/interval state, which can change from a key this same frame.
    update_toolbar();

    if (view_ != ViewKind::None && content_dirty_) {
      update_overlay();
      content_dirty_ = false;
    }

    // A pending confirmation owns the status line until it is answered, so the
    // prompt is not cleared out from under the user.
    if (confirm_pid_ == 0 && message_timer_ > 0.0) {
      message_timer_ -= delta_time;
      if (message_timer_ <= 0.0 && status_.get() != nullptr) {
        set_status(help_line());
      }
    }
  }

  bool on_input(const guayaba::InputEvent& event) override {
    if (event.type == guayaba::InputEvent::Type::MOUSE_WHEEL) {
      if (view_ == ViewKind::None && confirm_pid_ == 0 && table_.get() != nullptr &&
          table_.get()->is_inside(event.mouse_pos)) {
        // The wheel moves the cursor rather than the window: the cursor is the
        // scroll anchor, so the two can never drift apart.
        select(-event.wheel_delta);
        return true;
      }
      return false;
    }

    // A left press on a data row selects it. A press on a header Button is
    // consumed by the Button before it reaches this root script (controls
    // route before the root), so this only ever sees the rows.
    if (event.type == guayaba::InputEvent::Type::MOUSE_BUTTON) {
      if (view_ == ViewKind::None && confirm_pid_ == 0 &&
          event.mouse_button == guayaba::MouseButton::LEFT && event.pressed &&
          table_.get() != nullptr && table_.get()->is_inside(event.mouse_pos) &&
          table_controller_ != nullptr) {
        const bool hit = table_controller_->select_at(event.mouse_pos, visible_);
        // Clicking away from the search bar finishes the edit, so the next keystroke
        // is a hotkey again rather than another filter character.
        if (hit) blur_search();
        return hit;
      }
      return false;
    }

    if (event.type != guayaba::InputEvent::Type::KEY || !event.pressed) return false;

    // A pending force-kill confirmation swallows every key until it is answered,
    // so no other action can fire while the prompt is up.
    if (confirm_pid_ != 0) {
      if (event.key == guayaba::Key::Y) {
        on_confirm_yes();
        return true;
      }
      if (event.key == guayaba::Key::N || event.key == guayaba::Key::ESCAPE) {
        on_confirm_no();
        return true;
      }
      return true;
    }

    // While the input field holds the focus it consumes typing and Enter, so the
    // only key that reaches this root script is Escape, the cancel gesture.
    if (mode_ != InputMode::None) {
      if (event.key == guayaba::Key::ESCAPE) {
        cancel_input_mode();
        return true;
      }
      return false;
    }

    // A detail view is modal: its own ListView consumes the navigation keys and
    // Escape closes it. Everything else is swallowed, so table hotkeys cannot
    // fire against a process the user cannot see.
    if (view_ != ViewKind::None) {
      if (event.key == guayaba::Key::ESCAPE) close_overlay();
      return true;
    }

    // "/" is the usual "find" key; it focuses the same search bar f does. It has
    // no Key of its own, so it is recognised by the character it produces.
    if (event.text == U'/') {
      enter_filter();
      return true;
    }

    switch (event.key) {
      case guayaba::Key::Q:
        guayaba::SceneManager::get_instance().stop_running();
        return true;
      case guayaba::Key::S:
        cycle_sort_and_report();
        return true;
      case guayaba::Key::I:
        cycle_interval();
        return true;
      case guayaba::Key::K:
        // k terminates politely; Shift+K forces termination behind a separate
        // confirmation, because SIGKILL cannot be undone.
        if (event.shift || event.text == U'K') {
          request_force_kill();
        } else {
          terminate_selected(false);
        }
        return true;
      case guayaba::Key::Z:
        set_suspended(true);
        return true;
      case guayaba::Key::C:
        set_suspended(false);
        return true;
      case guayaba::Key::F:
        enter_filter();
        return true;
      case guayaba::Key::P:
        enter_priority();
        return true;
      case guayaba::Key::T:
        open_threads();
        return true;
      case guayaba::Key::R:
        open_tree();
        return true;
      case guayaba::Key::G:
        open_history();
        return true;
      case guayaba::Key::UP:
        select(-1);
        return true;
      case guayaba::Key::DOWN:
        select(1);
        return true;
      case guayaba::Key::PAGE_UP:
        select(-page_step());
        return true;
      case guayaba::Key::PAGE_DOWN:
        select(page_step());
        return true;
      case guayaba::Key::HOME:
        if (table_controller_ != nullptr) table_controller_->select_to(0, visible_);
        return true;
      case guayaba::Key::END:
        if (table_controller_ != nullptr) {
          table_controller_->select_to(static_cast<int>(visible_.size()), visible_);
        }
        return true;
      default:
        return false;
    }
  }

  const std::string& help_text() const { return help_text_; }

 private:
  void refresh() {
    SystemInfo system = source_->read_system();
    std::vector<ProcessInfo> processes = source_->read_processes();
    sampler_.apply(system, processes);

    if (has_previous_ && system.total_cpu_time_ns > previous_total_ns_) {
      const std::uint64_t total_delta = system.total_cpu_time_ns - previous_total_ns_;
      const std::uint64_t idle_delta =
          system.idle_cpu_time_ns > previous_idle_ns_
              ? system.idle_cpu_time_ns - previous_idle_ns_
              : 0;
      system_cpu_percent_ = total_delta > 0
                                ? static_cast<double>(total_delta - idle_delta) * 100.0 /
                                      static_cast<double>(total_delta)
                                : 0.0;
    }
    previous_total_ns_ = system.total_cpu_time_ns;
    previous_idle_ns_ = system.idle_cpu_time_ns;
    has_previous_ = true;

    system_ = system;
    processes_ = std::move(processes);
    record_history();
    content_dirty_ = true;
    update_summary();
  }

  // Appends this refresh's CPU% to each live process's series and forgets the
  // ones that exited. The history view is the only reader; keying by pid is what
  // lets a series survive the table's re-sorting.
  void record_history() {
    std::unordered_set<int> alive;
    alive.reserve(processes_.size());
    for (const ProcessInfo& proc : processes_) {
      alive.insert(proc.pid);
      std::deque<double>& series = history_[proc.pid];
      series.push_back(proc.cpu_percent);
      while (series.size() > kHistorySamples) series.pop_front();
    }
    for (auto it = history_.begin(); it != history_.end();) {
      if (alive.count(it->first) == 0) {
        it = history_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void update_summary() {
    if (summary_tasks_.get() != nullptr) {
      summary_tasks_.get()->set_text(fit_width(
          "Tasks: " + std::to_string(system_.total) + " total, " +
          std::to_string(system_.running) + " running, " +
          std::to_string(system_.stopped) + " stopped, " +
          std::to_string(system_.zombie) + " zombie"));
    }
    if (summary_mem_.get() != nullptr) {
      summary_mem_.get()->set_text(fit_width(
          "CPU: " + format_percent(system_cpu_percent_) + "% | Mem: " +
          format_bytes(system_.mem_total) + " total, " +
          format_bytes(system_.mem_used) + " used, " +
          format_bytes(system_.mem_available) + " avail | Load: " +
          format_percent(system_.load_avg[0]) + " " +
          format_percent(system_.load_avg[1]) + " " +
          format_percent(system_.load_avg[2]) +
          " | Interval: " + std::to_string(static_cast<int>(interval_)) + "s"));
    }
  }

  // Rebuilds the list the table shows: the snapshot with the name filter applied,
  // sorted by the active column. The snapshot itself is left intact, so an action
  // can still reach a process the filter is currently hiding.
  void rebuild_visible() {
    visible_.clear();
    visible_.reserve(processes_.size());
    for (const ProcessInfo& proc : processes_) {
      if (matches_filter(proc)) visible_.push_back(proc);
    }

    if (table_controller_ == nullptr) return;
    const std::string key = table_controller_->sort_key();
    const bool ascending = table_controller_->ascending();
    std::sort(visible_.begin(), visible_.end(),
              [&key, ascending](const ProcessInfo& a, const ProcessInfo& b) {
                // Primary key: the active column, in the active direction.
                const bool a_first =
                    ascending ? process_less(a, b, key) : process_less(b, a, key);
                const bool b_first =
                    ascending ? process_less(b, a, key) : process_less(a, b, key);
                if (a_first != b_first) return a_first;
                // Tie (e.g. every root process under USER): fall back to PID
                // ascending. PID is unique, so the comparator is a total order
                // and equivalent rows keep their position across refreshes
                // instead of reshuffling with whatever order readdir returned.
                return a.pid < b.pid;
              });
  }

  // Case-insensitive substring test, so a filter typed in any case matches.
  static bool contains_ci(const std::string& haystack, const std::string& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                       [](char a, char b) {
                         return std::tolower(static_cast<unsigned char>(a)) ==
                                std::tolower(static_cast<unsigned char>(b));
                       }) != haystack.end();
  }

  // Spec item 13: filter by name. The command and the user are both searched, so
  // "root" and "firefox" both work.
  bool matches_filter(const ProcessInfo& proc) const {
    if (filter_.empty()) return true;
    return contains_ci(proc.command, filter_) || contains_ci(proc.user, filter_);
  }

  // --- Search bar (filter / priority) ---------------------------------------

  void enter_filter() {
    mode_ = InputMode::Filter;
    if (prompt_.get() != nullptr) {
      prompt_.get()->set_placeholder(kSearchHint);
      prompt_.get()->set_text(filter_);
      prompt_.get()->grab_focus();
    }
    refresh_help();
  }

  void enter_priority() {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) {
      set_message("No process selected");
      return;
    }
    mode_ = InputMode::Priority;
    if (prompt_.get() != nullptr) {
      // Left empty rather than prefilled with the current nice: a prefilled value
      // would be edited at the caret, so typing "5" onto "0" would read 50. The
      // current value goes in the status line instead, and Enter applies what is
      // typed. The search text it displaces is restored on the way out.
      prompt_.get()->set_text("");
      prompt_.get()->grab_focus();
    }
    set_message("nice for PID " + std::to_string(proc->pid) + " (now " +
                std::to_string(proc->priority) + ", -20..19), Enter to apply");
  }

  void cancel_input_mode() {
    if (mode_ == InputMode::Filter) {
      filter_.clear();
      dirty_ = true;
    }
    leave_input_mode();
  }

  // Leaves the field and puts the search text back, so an abandoned priority
  // edit does not eat the filter.
  void leave_input_mode() {
    mode_ = InputMode::None;
    if (prompt_.get() != nullptr) {
      prompt_.get()->set_placeholder(kSearchHint);
      prompt_.get()->set_text(filter_);
      prompt_.get()->release_focus();
    }
    refresh_help();
  }

  // Clicking a button or a row while the field holds the focus ends the edit.
  // release_focus emits focus_exited, which is what actually resets the mode.
  void blur_search() {
    if (prompt_.get() != nullptr && prompt_.get()->is_focused()) {
      prompt_.get()->release_focus();
    }
  }

  void clear_filter() {
    filter_.clear();
    dirty_ = true;
    if (prompt_.get() != nullptr) prompt_.get()->set_text("");
    refresh_help();
  }

  void on_prompt_changed(const std::string& text) {
    if (mode_ == InputMode::Filter) {
      filter_ = text;
      dirty_ = true;  // live filtering as the user types
      refresh_help();
    }
  }

  void on_prompt_submitted(const std::string& text) {
    if (mode_ == InputMode::Priority) {
      const std::string result = apply_priority(text);
      leave_input_mode();
      if (!result.empty()) set_message(result);
    } else {
      filter_ = text;
      dirty_ = true;
      leave_input_mode();
    }
  }

  // The LineEdit takes the focus on click itself, so filtering follows the focus
  // rather than a key. The engine emits focus_entered/focus_exited but does not
  // declare them, and connecting to an undeclared signal throws, so the focus is
  // polled here instead: a click is the mouse path into filtering.
  void sync_search_focus() {
    if (prompt_.get() == nullptr) return;
    const bool focused = prompt_.get()->is_focused();
    if (focused && mode_ == InputMode::None) {
      mode_ = InputMode::Filter;
      prompt_.get()->set_placeholder(kSearchHint);
      refresh_help();
    } else if (!focused && mode_ != InputMode::None) {
      // The edit is over (focus moved away or was released): keep whatever was
      // typed as the filter and put the search text back.
      mode_ = InputMode::None;
      prompt_.get()->set_placeholder(kSearchHint);
      prompt_.get()->set_text(filter_);
      refresh_help();
    }
  }

  // Spec item 10: change the priority of the cursor's process. The field is
  // prefilled with the current nice, so Enter alone is a no-op and an edit is
  // whatever the user types.
  std::string apply_priority(const std::string& text) {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) return "No process selected";

    int value = 0;
    try {
      std::size_t used = 0;
      value = std::stoi(text, &used);
      if (used != text.size()) return "Invalid priority: " + text;
    } catch (const std::exception&) {
      return "Invalid priority: " + text;
    }
    value = std::max(-20, std::min(19, value));  // the nice range

    const int pid = proc->pid;
    if (source_->set_priority(pid, value)) {
      elapsed_ = interval_;  // re-read now so PRI shows the change
      return "nice " + std::to_string(value) + " -> PID " + std::to_string(pid);
    }
    return report_failure(pid);
  }

  // The line the status bar falls back to when no message is showing: the live
  // filter, if any, then the hotkeys. The filter goes first so a long hotkey
  // list can never clip it off the right edge.
  std::string help_line() const {
    if (filter_.empty()) return help_text_;
    return "filter:" + filter_ + "  \u2502  " + help_text_;
  }

  void refresh_help() {
    if (status_.get() == nullptr || confirm_pid_ != 0) return;
    set_status(help_line());
    message_timer_ = 0.0;
  }

  std::string sort_description() const {
    if (table_controller_ == nullptr) return "";
    return table_controller_->sort_key() +
           (table_controller_->ascending() ? " asc" : " desc");
  }

  void cycle_interval() {
    static const double kPresets[] = {1.0, 2.0, 5.0, 10.0};
    const int count = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
    interval_index_ = (interval_index_ + 1) % count;
    interval_ = kPresets[interval_index_];
    update_summary();
    set_message("Refresh interval: " + std::to_string(static_cast<int>(interval_)) + "s");
  }

  void select(int rows) {
    if (table_controller_ != nullptr) {
      table_controller_->select_by(rows, visible_);
    }
  }

  // Mirrors the highlighted row into its own label, which is also where the
  // actions and the priority editor will read their target from. The command is
  // truncated: a long path would make the label's minimum wider than the row and
  // shove the action buttons past the right edge.
  void update_selection() {
    if (selection_.get() == nullptr) return;
    std::string text = "Selected: -";
    if (table_controller_ != nullptr) {
      const int index = table_controller_->selected_index();
      if (index >= 0 && index < static_cast<int>(visible_.size())) {
        const ProcessInfo& proc = visible_[static_cast<std::size_t>(index)];
        text = "Selected PID " + std::to_string(proc.pid) + "  " +
               truncate(proc.command, 24);
      }
    }
    if (text != selection_text_) {
      selection_.get()->set_text(text);
      selection_text_ = text;
    }
  }

  // Shortens `text` to at most `max` bytes, ending in an ellipsis when it had to
  // cut. Byte-based on purpose: it bounds the label's minimum width, which is
  // what the layout needs, and commands are almost always ASCII.
  static std::string truncate(const std::string& text, std::size_t max) {
    if (text.size() <= max) return text;
    return text.substr(0, max) + "\u2026";
  }

  // `text` clipped to at most `max_cells` codepoints, so a multi-byte glyph is
  // never cut in half.
  static std::string clip_cells(const std::string& text, int max_cells) {
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

  // Keeps a free-form line from widening the layout. A fit_content label whose
  // text is wider than the screen grows the whole layout VBox, which pushes the
  // toolbar's right-aligned buttons past the right edge; the help/status line is
  // the one that can get long (it carries the hotkeys and the active filter).
  std::string fit_width(const std::string& text) const {
    const int cols = table_controller_ ? table_controller_->screen_cols() : 0;
    if (cols <= 4) return text;
    return clip_cells(text, cols - 2);
  }

  void set_status(const std::string& text) {
    if (status_.get() != nullptr) status_.get()->set_text(fit_width(text));
  }

  // The row under the cursor, or nullptr when there is none. Every action is
  // aimed through this one accessor.
  const ProcessInfo* selected_process() const {
    if (table_controller_ == nullptr) return nullptr;
    const int index = table_controller_->selected_index();
    if (index < 0 || index >= static_cast<int>(visible_.size())) return nullptr;
    return &visible_[static_cast<std::size_t>(index)];
  }

  void terminate_selected(bool force) {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) {
      set_message("No process selected");
      return;
    }
    terminate_pid(proc->pid, force);
  }

  void terminate_pid(int pid, bool force) {
    if (source_->terminate(pid, force)) {
      set_message(std::string(force ? "SIGKILL" : "SIGTERM") + " -> PID " +
                  std::to_string(pid));
      elapsed_ = interval_;  // re-read now so the row goes away this frame
    } else {
      set_message(report_failure(pid));
    }
  }

  void request_force_kill() {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) {
      set_message("No process selected");
      return;
    }
    open_confirm(proc->pid, true);
  }

  // Force kill cannot be undone, so it asks first. The confirmation is a small
  // modal overlay with Yes/No buttons - the mouse path - while y/n/Escape still
  // work for the keyboard. Built like the detail views (an overlay added to the
  // root and queue_free()d), and confirm_pid_ doubles as "a prompt is up".
  void open_confirm(int pid, bool force) {
    close_confirm();
    confirm_pid_ = pid;
    confirm_force_ = force;

    std::string command;
    for (const ProcessInfo& proc : processes_) {
      if (proc.pid == pid) {
        command = proc.command;
        break;
      }
    }

    guayaba::NodeRef<guayaba::PanelContainer> overlay =
        node->add_child<guayaba::PanelContainer>("confirm");
    overlay->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    overlay->set_clip_children(true);
    overlay->get_stylebox().set_background_color(guayaba::Color::BLACK);
    overlay->get_stylebox().set_content_margin(0, 0, 0, 0);

    // VBox(Center) + HBox(Center) is what centres a small box on both axes: a
    // container centres its block along its own main axis, so the nesting does it.
    guayaba::NodeRef<guayaba::VBox> outer =
        overlay->add_child<guayaba::VBox>("confirm_outer");
    outer->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    outer->set_content_alignment(guayaba::ContentAlignment::Center);

    guayaba::NodeRef<guayaba::HBox> middle =
        outer->add_child<guayaba::HBox>("confirm_middle");
    middle->set_content_alignment(guayaba::ContentAlignment::Center);

    guayaba::NodeRef<guayaba::PanelContainer> box =
        middle->add_child<guayaba::PanelContainer>("confirm_box");
    box->set_custom_minimum_size(guayaba::Vec2{54, 0});
    box->get_stylebox().set_background_color(guayaba::Color::BLACK);
    box->get_stylebox().set_border_style(guayaba::StyleBorder::DOUBLE);
    box->get_stylebox().set_border_width(1);
    box->get_stylebox().set_border_color(guayaba::Color::BRIGHT_RED);
    box->get_stylebox().set_content_margin(1, 0, 1, 0);

    guayaba::NodeRef<guayaba::VBox> inner =
        box->add_child<guayaba::VBox>("confirm_inner");
    inner->set_content_alignment(guayaba::ContentAlignment::Center);
    inner->set_separation(1);

    guayaba::NodeRef<guayaba::Label> message =
        inner->add_child<guayaba::Label>("confirm_message");
    message->set_fit_content(true);
    message->set_text_color(guayaba::Color::BRIGHT_WHITE);
    message->set_text(std::string(force ? "Force kill" : "Terminate") + " PID " +
                      std::to_string(pid) +
                      (command.empty() ? "" : " (" + truncate(command, 32) + ")") +
                      "?");

    guayaba::NodeRef<guayaba::HBox> buttons =
        inner->add_child<guayaba::HBox>("confirm_buttons");
    buttons->set_content_alignment(guayaba::ContentAlignment::Center);
    buttons->set_separation(2);

    guayaba::NodeRef<guayaba::Button> yes =
        tasks_lens::add_tool_button(buttons, "confirm_yes", "Yes");
    guayaba::NodeRef<guayaba::Button> no =
        tasks_lens::add_tool_button(buttons, "confirm_no", "No");
    connect_clicked(yes, &Monitor::on_confirm_yes);
    connect_clicked(no, &Monitor::on_confirm_no);

    confirm_overlay_ = overlay;
  }

  void close_confirm() {
    confirm_pid_ = 0;
    confirm_force_ = false;
    if (confirm_overlay_.get() != nullptr) confirm_overlay_.get()->queue_free();
    confirm_overlay_ = {};
  }

  void on_confirm_yes() {
    const int pid = confirm_pid_;
    const bool force = confirm_force_;
    close_confirm();
    if (pid != 0) terminate_pid(pid, force);
  }

  void on_confirm_no() {
    close_confirm();
    set_message("Cancelled");
  }

  void set_suspended(bool suspend) {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) {
      set_message("No process selected");
      return;
    }
    const int pid = proc->pid;
    const bool ok = suspend ? source_->suspend(pid) : source_->resume(pid);
    if (ok) {
      set_message(std::string(suspend ? "SIGSTOP" : "SIGCONT") + " -> PID " +
                  std::to_string(pid) +
                  (suspend ? " (suspended)" : " (resumed)"));
      elapsed_ = interval_;  // re-read now so STATE shows the change
    } else {
      set_message(report_failure(pid));
    }
  }

  // Spec item 15: a process that vanished, or one this user cannot signal, is a
  // message rather than a crash. One bool cannot tell the two apart, so the
  // wording covers both.
  std::string report_failure(int pid) const {
    return "PID " + std::to_string(pid) + ": failed (gone or no permission)";
  }

  // --- Toolbar buttons ------------------------------------------------------

  void connect_clicked(guayaba::NodeRef<guayaba::Button>& btn, void (Monitor::*slot)()) {
    if (btn.get() != nullptr) connect_script_signal("clicked", btn.get(), this, slot);
  }

  void cycle_sort_and_report() {
    if (table_controller_ != nullptr) table_controller_->cycle_sort();
    set_message("Sort: " + sort_description());
  }

  // Every handler blurs the search first, so a click finishes an in-progress edit
  // and the next keystroke is a hotkey rather than a filter character.
  void on_button_sort() { blur_search(); cycle_sort_and_report(); }
  void on_button_interval() { blur_search(); cycle_interval(); }
  void on_button_quit() { guayaba::SceneManager::get_instance().stop_running(); }
  void on_button_clear() { blur_search(); clear_filter(); }
  void on_button_kill() { blur_search(); terminate_selected(false); }
  void on_button_force() { blur_search(); request_force_kill(); }
  void on_button_stop() { blur_search(); set_suspended(true); }
  void on_button_cont() { blur_search(); set_suspended(false); }
  void on_button_nice() { blur_search(); enter_priority(); }
  void on_button_threads() { blur_search(); open_threads(); }
  void on_button_tree() { blur_search(); open_tree(); }
  void on_button_history() { blur_search(); open_history(); }
  void on_button_overlay_close() { close_overlay(); }

  // Runs every frame: greys the process actions when nothing is selected, and
  // keeps the Sort/Interval labels current. The text setters are guarded so a
  // Button is not re-wrapped 30 times a second.
  void update_toolbar() {
    const bool has_selection = selected_process() != nullptr;
    set_enabled(toolbar_.kill, has_selection);
    set_enabled(toolbar_.force, has_selection);
    set_enabled(toolbar_.stop, has_selection);
    set_enabled(toolbar_.cont, has_selection);
    set_enabled(toolbar_.nice, has_selection);
    set_enabled(toolbar_.threads, has_selection);
    set_enabled(toolbar_.tree, has_selection);
    set_enabled(toolbar_.history, has_selection);

    const std::string sort_label = sort_button_label();
    if (sort_label != last_sort_label_ && toolbar_.sort.get() != nullptr) {
      last_sort_label_ = sort_label;
      toolbar_.sort.get()->set_text(sort_label);
    }
    const std::string interval_label = interval_button_label();
    if (interval_label != last_interval_label_ && toolbar_.interval.get() != nullptr) {
      last_interval_label_ = interval_label;
      toolbar_.interval.get()->set_text(interval_label);
    }
  }

  static void set_enabled(guayaba::NodeRef<guayaba::Button>& btn, bool enabled) {
    if (btn.get() != nullptr) btn.get()->set_disabled(!enabled);
  }

  std::string sort_button_label() const {
    if (table_controller_ == nullptr) return "Sort";
    std::string key = table_controller_->sort_key();
    for (char& ch : key) {
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return "Sort: " + key + (table_controller_->ascending() ? " \u2191" : " \u2193");
  }

  std::string interval_button_label() const {
    return "Every " + std::to_string(static_cast<int>(interval_)) + "s";
  }

  // --- Detail views (threads, tree, history) --------------------------------
  //
  // The engine has no node visibility, so a view is an overlay: a full-screen
  // panel added to the root on demand and queue_free()d on Escape. It draws on
  // top (last child) and routes input first, so the table and its hotkeys are
  // covered until it is closed. Built here rather than in the scene because it
  // exists only while a view is open.

  void open_threads() { open_process_view(ViewKind::Threads); }
  void open_tree() { open_process_view(ViewKind::Tree); }

  void open_history() { open_process_view(ViewKind::History); }

  void open_process_view(ViewKind kind) {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) {
      set_message("No process selected");
      return;
    }
    open_overlay(kind, proc->pid);
  }

  void open_overlay(ViewKind kind, int pid) {
    close_overlay();
    view_ = kind;
    detail_pid_ = pid;

    guayaba::NodeRef<guayaba::PanelContainer> panel =
        node->add_child<guayaba::PanelContainer>("overlay");
    panel->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    panel->set_clip_children(true);
    panel->get_stylebox().set_background_color(guayaba::Color::BLACK);
    panel->get_stylebox().set_border_style(guayaba::StyleBorder::DOUBLE);
    panel->get_stylebox().set_border_width(1);
    panel->get_stylebox().set_border_color(guayaba::Color::BRIGHT_CYAN);
    panel->get_stylebox().set_content_margin(0, 0, 0, 0);

    guayaba::NodeRef<guayaba::VBox> box =
        panel->add_child<guayaba::VBox>("overlay_layout");
    box->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    box->set_content_alignment(guayaba::ContentAlignment::Begin);
    box->set_separation(1);

    guayaba::NodeRef<guayaba::HBox> header =
        box->add_child<guayaba::HBox>("overlay_header");
    header->set_content_alignment(guayaba::ContentAlignment::Begin);
    header->set_separation(1);
    // Fill the overlay's width so the title can grow and push Close to the right
    // edge; without it the header is only as wide as its children.
    header->set_anchor_right(1.0f);
    header->set_grow_x(guayaba::Grow::Both);

    guayaba::NodeRef<guayaba::Label> title =
        header->add_child<guayaba::Label>("overlay_title");
    title->set_fit_content(true);
    title->set_text_color(guayaba::Color::BRIGHT_CYAN);
    title->set_anchor_right(1.0f);
    title->set_grow_x(guayaba::Grow::Both);

    guayaba::NodeRef<guayaba::Button> close =
        tasks_lens::add_tool_button(header, "overlay_close", "Close");
    connect_clicked(close, &Monitor::on_button_overlay_close);
    overlay_close_ = close;

    if (kind == ViewKind::History) {
      guayaba::NodeRef<guayaba::Graph> graph =
          box->add_child<guayaba::Graph>("overlay_graph");
      graph->set_anchor_preset(guayaba::AnchorPreset::FullRect);
      graph->set_grow_y(guayaba::Grow::Both);
      graph->set_line_color(guayaba::Color::BRIGHT_GREEN);
      // Auto-range: a single process sits near 0% of the whole machine, so a
      // fixed 0..100 span would flatten its own variation below one cell. The
      // Graph frames the samples it has, which is what shows the shape.
      overlay_graph_ = graph;
    } else {
      guayaba::NodeRef<guayaba::ListView> list =
          box->add_child<guayaba::ListView>("overlay_list");
      list->set_anchor_preset(guayaba::AnchorPreset::FullRect);
      list->set_grow_y(guayaba::Grow::Both);
      list->set_text_color(guayaba::Color::WHITE);
      list->grab_focus();
      overlay_list_ = list;
    }

    overlay_ = panel;
    overlay_title_ = title;
    content_dirty_ = true;
    update_overlay();
  }

  void close_overlay() {
    view_ = ViewKind::None;
    detail_pid_ = 0;
    if (overlay_.get() != nullptr) overlay_.get()->queue_free();
    overlay_ = {};
    overlay_title_ = {};
    overlay_list_ = {};
    overlay_graph_ = {};
    overlay_close_ = {};
  }

  // Refills the open view from the data. Called on open and whenever the
  // snapshot changes, so a stopped thread's state or a new sample shows up.
  void update_overlay() {
    if (view_ == ViewKind::None) return;

    if (view_ == ViewKind::Threads) {
      const std::vector<ThreadInfo> threads = source_->read_threads(detail_pid_);
      std::vector<std::string> items;
      items.reserve(threads.size());
      for (const ThreadInfo& thread : threads) items.push_back(thread_line(thread));
      if (overlay_title_.get() != nullptr) {
        overlay_title_.get()->set_text("PID " + std::to_string(detail_pid_) + ": " +
                                       std::to_string(items.size()) +
                                       " threads   [Esc] back");
      }
      if (overlay_list_.get() != nullptr) overlay_list_.get()->set_items(std::move(items));
      return;
    }

    if (view_ == ViewKind::Tree) {
      std::vector<std::string> items = tree_lines();
      if (overlay_title_.get() != nullptr) {
        overlay_title_.get()->set_text("Process tree, " + std::to_string(items.size()) +
                                       " processes   [Esc] back");
      }
      if (overlay_list_.get() != nullptr) overlay_list_.get()->set_items(std::move(items));
      return;
    }

    if (overlay_title_.get() != nullptr) {
      overlay_title_.get()->set_text("CPU history, PID " + std::to_string(detail_pid_) +
                                     "   [Esc] back");
    }
    if (overlay_graph_.get() != nullptr) {
      const auto it = history_.find(detail_pid_);
      if (it != history_.end()) {
        overlay_graph_.get()->set_values(
            std::vector<double>(it->second.begin(), it->second.end()));
      } else {
        overlay_graph_.get()->set_values({});
      }
    }
  }

  std::string thread_line(const ThreadInfo& thread) const {
    return pad(std::to_string(thread.tid), 7) + " " + process_state_code(thread.state) +
           "  " + format_cpu_time(thread.cpu_time_ns) + "  " + thread.name;
  }

  // The whole machine as an indented parent -> child list: the same information
  // the table has, ordered by the hierarchy instead of by a column. Roots are the
  // processes whose parent is not in the snapshot; anything left (a cycle, or a
  // parent that vanished mid-read) is shown at depth zero so nothing is lost.
  std::vector<std::string> tree_lines() const {
    std::unordered_map<int, const ProcessInfo*> by_pid;
    std::unordered_map<int, std::vector<int>> children;
    for (const ProcessInfo& proc : processes_) by_pid[proc.pid] = &proc;
    for (const ProcessInfo& proc : processes_) {
      if (proc.ppid != proc.pid) children[proc.ppid].push_back(proc.pid);
    }
    for (auto& entry : children) std::sort(entry.second.begin(), entry.second.end());

    std::vector<std::string> lines;
    lines.reserve(processes_.size());
    std::unordered_set<int> visited;
    for (const ProcessInfo& proc : processes_) {
      if (by_pid.find(proc.ppid) == by_pid.end()) {
        append_subtree(proc.pid, 0, children, by_pid, visited, lines);
      }
    }
    for (const ProcessInfo& proc : processes_) {
      append_subtree(proc.pid, 0, children, by_pid, visited, lines);
    }
    return lines;
  }

  static void append_subtree(
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

  int page_step() const {
    if (table_controller_ == nullptr) return 1;
    return std::max(1, table_controller_->capacity() - 1);
  }

  void set_message(const std::string& text) {
    set_status(text);
    message_timer_ = 2.0;
  }

  std::unique_ptr<ProcessSource> source_;
  CpuSampler sampler_;

  guayaba::NodeRef<guayaba::Label> summary_tasks_;
  guayaba::NodeRef<guayaba::Label> summary_mem_;
  guayaba::NodeRef<guayaba::Label> selection_;
  std::string selection_text_;
  guayaba::NodeRef<guayaba::Label> status_;
  guayaba::NodeRef<guayaba::LineEdit> prompt_;
  MonitorToolbar toolbar_;
  guayaba::NodeRef<guayaba::PanelContainer> table_;
  TableController* table_controller_ = nullptr;

  InputMode mode_ = InputMode::None;
  std::string filter_;

  // The open detail view and its nodes. All empty when no view is up.
  ViewKind view_ = ViewKind::None;
  int detail_pid_ = 0;
  guayaba::NodeRef<guayaba::PanelContainer> overlay_;
  guayaba::NodeRef<guayaba::Label> overlay_title_;
  guayaba::NodeRef<guayaba::ListView> overlay_list_;
  guayaba::NodeRef<guayaba::Graph> overlay_graph_;
  guayaba::NodeRef<guayaba::Button> overlay_close_;

  // The force-kill confirmation popup, and whether it is a force (vs a plain
  // terminate). confirm_pid_ below doubles as "a prompt is up".
  guayaba::NodeRef<guayaba::PanelContainer> confirm_overlay_;
  bool confirm_force_ = false;

  // One capped CPU% series per pid, for the history view (spec 7). Keyed by pid
  // so it survives the table's re-sorting, and pruned when a process exits.
  static constexpr std::size_t kHistorySamples = 120;
  std::unordered_map<int, std::deque<double>> history_;

  SystemInfo system_;
  // The full snapshot, filtered and sorted into `visible_` only when something
  // that changes the view does - see on_process.
  std::vector<ProcessInfo> processes_;
  std::vector<ProcessInfo> visible_;
  bool dirty_ = true;
  // Set on every snapshot so an open detail view refills from fresh data.
  bool content_dirty_ = true;
  std::string last_sort_key_;
  bool last_ascending_ = true;
  // Guards for the toolbar's live labels, so set_text only runs on a real change.
  std::string last_sort_label_;
  std::string last_interval_label_;

  double interval_ = 1.0;
  int interval_index_ = 0;
  double elapsed_ = 0.0;
  bool first_frame_ = true;

  double system_cpu_percent_ = 0.0;
  std::uint64_t previous_total_ns_ = 0;
  std::uint64_t previous_idle_ns_ = 0;
  bool has_previous_ = false;

  double message_timer_ = 0.0;
  // PID awaiting a confirmation; 0 when no prompt is up. Set by open_confirm()
  // and read by both the popup's buttons and the y/n/Escape keys.
  int confirm_pid_ = 0;

  // The search bar's resting hint, shown while the field is empty.
  static constexpr const char* kSearchHint = "name or user\u2026";
  std::string help_text_ =
      "[/] Search  [s] Sort  [i] Int  [\u2191\u2193] Move  [p] Nice  [t] Thr  [r] Tree  [g] Hist  "
      "[k] Kill  [K] Force  [z] Stop  [c] Cont  [q] Quit";
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_MONITOR_H
