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

#include <guayaba/Guayaba.hpp>

#include "Data/cpu_sampler.h"
#include "Data/process_info.h"
#include "Data/process_source.h"
#include "Scripts/format.h"
#include "Scripts/table_controller.h"

namespace tasks_lens {

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
          guayaba::NodeRef<guayaba::PanelContainer> table,
          TableController* table_controller)
      : source_(std::move(source)),
        summary_tasks_(summary_tasks),
        summary_mem_(summary_mem),
        selection_(selection),
        status_(status),
        prompt_(prompt),
        table_(table),
        table_controller_(table_controller) {}

  // Wires the input field. Done here rather than in the scene builder because
  // the Monitor is the one that knows what a submitted value means.
  void on_attached() override {
    if (prompt_.get() != nullptr) {
      connect_script_signal("text_changed", prompt_.get(), this, &Monitor::on_prompt_changed);
      connect_script_signal("text_submitted", prompt_.get(), this, &Monitor::on_prompt_submitted);
    }
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

    if (table_controller_ != nullptr) table_controller_->render(visible_);
    update_selection();

    if (view_ != ViewKind::None && content_dirty_) {
      update_overlay();
      content_dirty_ = false;
    }

    // A pending confirmation owns the status line until it is answered, so the
    // prompt is not cleared out from under the user.
    if (confirm_pid_ == 0 && message_timer_ > 0.0) {
      message_timer_ -= delta_time;
      if (message_timer_ <= 0.0 && status_.get() != nullptr) {
        status_.get()->set_text(help_line());
      }
    }
  }

  bool on_input(const guayaba::InputEvent& event) override {
    if (event.type == guayaba::InputEvent::Type::MOUSE_WHEEL) {
      if (view_ == ViewKind::None && table_.get() != nullptr &&
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
      if (view_ == ViewKind::None && event.mouse_button == guayaba::MouseButton::LEFT &&
          event.pressed && table_.get() != nullptr &&
          table_.get()->is_inside(event.mouse_pos) && table_controller_ != nullptr) {
        return table_controller_->select_at(event.mouse_pos, visible_);
      }
      return false;
    }

    if (event.type != guayaba::InputEvent::Type::KEY || !event.pressed) return false;

    // A pending force-kill confirmation swallows every key until it is answered,
    // so no other action can fire while the prompt is up.
    if (confirm_pid_ != 0) {
      if (event.key == guayaba::Key::Y) {
        const int pid = confirm_pid_;
        confirm_pid_ = 0;
        terminate_pid(pid, true);
        return true;
      }
      if (event.key == guayaba::Key::N || event.key == guayaba::Key::ESCAPE) {
        confirm_pid_ = 0;
        set_message("Cancelled");
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

    switch (event.key) {
      case guayaba::Key::Q:
        guayaba::SceneManager::get_instance().stop_running();
        return true;
      case guayaba::Key::S:
        if (table_controller_ != nullptr) table_controller_->cycle_sort();
        set_message("Sort: " + sort_description());
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
      summary_tasks_.get()->set_text(
          "Tasks: " + std::to_string(system_.total) + " total, " +
          std::to_string(system_.running) + " running, " +
          std::to_string(system_.stopped) + " stopped, " +
          std::to_string(system_.zombie) + " zombie");
    }
    if (summary_mem_.get() != nullptr) {
      summary_mem_.get()->set_text(
          "CPU: " + format_percent(system_cpu_percent_) + "% | Mem: " +
          format_bytes(system_.mem_total) + " total, " +
          format_bytes(system_.mem_used) + " used, " +
          format_bytes(system_.mem_available) + " avail | Load: " +
          format_percent(system_.load_avg[0]) + " " +
          format_percent(system_.load_avg[1]) + " " +
          format_percent(system_.load_avg[2]) +
          " | Interval: " + std::to_string(static_cast<int>(interval_)) + "s");
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

  // --- Input line (filter / priority) ---------------------------------------

  void enter_filter() {
    mode_ = InputMode::Filter;
    if (prompt_.get() != nullptr) {
      prompt_.get()->set_placeholder("");
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
      // current value goes in the prompt instead, and Enter applies what is typed.
      prompt_.get()->set_placeholder("");
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

  void leave_input_mode() {
    mode_ = InputMode::None;
    if (prompt_.get() != nullptr) {
      prompt_.get()->release_focus();
      prompt_.get()->set_text("");
      prompt_.get()->set_placeholder("[/] filter   [p] priority");
    }
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
    status_.get()->set_text(help_line());
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
  // actions and the priority editor will read their target from.
  void update_selection() {
    if (selection_.get() == nullptr) return;
    std::string text = "Selected: -";
    if (table_controller_ != nullptr) {
      const int index = table_controller_->selected_index();
      if (index >= 0 && index < static_cast<int>(visible_.size())) {
        const ProcessInfo& proc = visible_[static_cast<std::size_t>(index)];
        text = "Selected PID " + std::to_string(proc.pid) + "  " + proc.command;
      }
    }
    if (text != selection_text_) {
      selection_.get()->set_text(text);
      selection_text_ = text;
    }
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
    confirm_pid_ = proc->pid;
    set_message("SIGKILL PID " + std::to_string(proc->pid) + " (" + proc->command +
                ")? [y/N]");
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

    guayaba::NodeRef<guayaba::Label> title =
        box->add_child<guayaba::Label>("overlay_title");
    title->set_fit_content(true);
    title->set_text_color(guayaba::Color::BRIGHT_CYAN);

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
    if (status_.get() != nullptr) status_.get()->set_text(text);
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

  double interval_ = 1.0;
  int interval_index_ = 0;
  double elapsed_ = 0.0;
  bool first_frame_ = true;

  double system_cpu_percent_ = 0.0;
  std::uint64_t previous_total_ns_ = 0;
  std::uint64_t previous_idle_ns_ = 0;
  bool has_previous_ = false;

  double message_timer_ = 0.0;
  // PID awaiting a y/n answer for a forced termination; 0 when no prompt is up.
  int confirm_pid_ = 0;
  std::string help_text_ =
      "q Quit  s Sort  i Int  \u2191\u2193 Move  f Filter  p Nice  t Threads  "
      "r Tree  g Hist  k Kill  K Force  z Stop  c Cont";
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_MONITOR_H
