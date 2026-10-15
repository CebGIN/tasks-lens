#ifndef TASKS_LENS_SCRIPTS_MONITOR_H
#define TASKS_LENS_SCRIPTS_MONITOR_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "Data/cpu_sampler.h"
#include "Data/history_store.h"
#include "Data/process_info.h"
#include "Data/process_source.h"
#include "Data/process_tree.h"
#include "Scripts/confirm_dialog.h"
#include "Scripts/detail_overlay.h"
#include "Scripts/format.h"
#include "Scripts/priority_input.h"
#include "Scripts/process_sort.h"
#include "Scripts/table_controller.h"
#include "Scripts/text_util.h"
#include "Scripts/viewport.h"

namespace tasks_lens {

// The action buttons the Monitor wires and toggles.
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
    Root-node script. Owns the process data, the CPU sampler and the hotkeys:
    refreshes the snapshot on the interval, keeps the filtered and sorted visible
    list, and routes keyboard/mouse input to the table and the overlays.
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

  // Wires the input field and every toolbar button.
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
    // snapshot, a filter edit, or a sort change.
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
    // the sort/interval state.
    update_toolbar();

    if (view_ != ViewKind::None && content_dirty_) {
      update_overlay();
      content_dirty_ = false;
    }

    // A pending confirmation owns the status line until it is answered.
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
        // The wheel moves the cursor, which is the scroll anchor.
        select(-event.wheel_delta);
        return true;
      }
      return false;
    }

    // A left press on a data row selects it; a header Button consumes its own
    // presses before they reach this root script.
    if (event.type == guayaba::InputEvent::Type::MOUSE_BUTTON) {
      if (view_ == ViewKind::None && confirm_pid_ == 0 &&
          event.mouse_button == guayaba::MouseButton::LEFT && event.pressed &&
          table_.get() != nullptr && table_.get()->is_inside(event.mouse_pos) &&
          table_controller_ != nullptr) {
        const bool hit = table_controller_->select_at(event.mouse_pos, visible_);
        // Clicking away from the search bar finishes the edit.
        if (hit) blur_search();
        return hit;
      }
      return false;
    }

    if (event.type != guayaba::InputEvent::Type::KEY || !event.pressed) return false;

    // A pending confirmation swallows every key until it is answered.
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

    // While the input field holds focus, the only key that reaches this root
    // script is Escape, the cancel gesture.
    if (mode_ != InputMode::None) {
      if (event.key == guayaba::Key::ESCAPE) {
        cancel_input_mode();
        return true;
      }
      return false;
    }

    // A detail view is modal: its own ListView consumes the navigation keys,
    // Escape closes it, and every other key is swallowed.
    if (view_ != ViewKind::None) {
      if (event.key == guayaba::Key::ESCAPE) close_overlay();
      return true;
    }

    // "/" focuses the search bar; it has no Key of its own, so it is recognised
    // by the character it produces.
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
        // confirmation.
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

    system_ = system;
    processes_ = std::move(processes);
    history_.record(processes_);
    content_dirty_ = true;
    update_summary();
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
          "CPU: " + format_percent(sampler_.system_cpu_percent()) + "% | Mem: " +
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
  // sorted by the active column. The snapshot itself is left intact.
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
                // Tie: fall back to PID ascending. PID is unique, so the
                // comparator is a total order and rows keep their position
                // across refreshes.
                return a.pid < b.pid;
              });
  }

  // Case-insensitive substring test.
  static bool contains_ci(const std::string& haystack, const std::string& needle) {
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                       [](char a, char b) {
                         return std::tolower(static_cast<unsigned char>(a)) ==
                                std::tolower(static_cast<unsigned char>(b));
                       }) != haystack.end();
  }

  // Filter by name: matches the filter against the command and the user.
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
      // Left empty: a prefilled value would be edited at the caret. The current
      // value goes in the status line instead.
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

  // Leaves the field and puts the search text back.
  void leave_input_mode() {
    mode_ = InputMode::None;
    if (prompt_.get() != nullptr) {
      prompt_.get()->set_placeholder(kSearchHint);
      prompt_.get()->set_text(filter_);
      prompt_.get()->release_focus();
    }
    refresh_help();
  }

  // Clicking a button or a row while the field holds the focus ends the edit;
  // release_focus emits focus_exited, which is what resets the mode.
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

  // The LineEdit takes the focus on click itself, so filtering follows the focus,
  // not a key. The focus is polled because focus_entered/focus_exited are not
  // declared signals.
  void sync_search_focus() {
    if (prompt_.get() == nullptr) return;
    const bool focused = prompt_.get()->is_focused();
    if (focused && mode_ == InputMode::None) {
      mode_ = InputMode::Filter;
      prompt_.get()->set_placeholder(kSearchHint);
      refresh_help();
    } else if (!focused && mode_ != InputMode::None) {
      // The edit is over: keep what was typed as the filter and put the search
      // text back.
      mode_ = InputMode::None;
      prompt_.get()->set_placeholder(kSearchHint);
      prompt_.get()->set_text(filter_);
      refresh_help();
    }
  }

  // Applies the priority typed in the prompt to the selected process and returns
  // a status message.
  std::string apply_priority(const std::string& text) {
    const ProcessInfo* proc = selected_process();
    if (proc == nullptr) return "No process selected";

    const std::optional<int> value = parse_priority(text);
    if (!value) return "Invalid priority: " + text;

    const int pid = proc->pid;
    if (source_->set_priority(pid, *value)) {
      elapsed_ = interval_;  // re-read now so PRI shows the change
      return "nice " + std::to_string(*value) + " -> PID " + std::to_string(pid);
    }
    return report_failure(pid);
  }

  // The line the status bar falls back to when no message is showing: the live
  // filter, if any, then the hotkeys.
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

  // Mirrors the highlighted row into its own label. The command is truncated so
  // a long path cannot widen the row and push the action buttons off the edge.
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

  // Clips a free-form line to the screen width so a fit_content label cannot
  // widen the layout and push the toolbar's right-aligned buttons off the edge.
  std::string fit_width(const std::string& text) const {
    const int cols = terminal_cols();
    if (cols <= 4) return text;
    return clip_cells(text, cols - 2);
  }

  void set_status(const std::string& text) {
    if (status_.get() != nullptr) status_.get()->set_text(fit_width(text));
  }

  // The row under the cursor, or nullptr when there is none.
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

  // Shows the force-kill/terminate confirmation for `pid`; confirm_pid_ doubles
  // as "a prompt is up".
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
    const std::string message =
        std::string(force ? "Force kill" : "Terminate") + " PID " +
        std::to_string(pid) +
        (command.empty() ? "" : " (" + truncate(command, 32) + ")") + "?";

    ConfirmDialog dialog = build_confirm_dialog(node, message);
    connect_clicked(dialog.yes, &Monitor::on_confirm_yes);
    connect_clicked(dialog.no, &Monitor::on_confirm_no);
    confirm_overlay_ = dialog.overlay;
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

  // Message for a failed signal: one bool cannot tell "gone" from "no permission",
  // so the wording covers both.
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

  // Every handler blurs the search first, so a click finishes an in-progress edit.
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

  // Runs every frame: greys the process actions when nothing is selected and keeps
  // the Sort/Interval labels current.
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
  // A view is a full-screen overlay added to the root on demand and freed on
  // Escape; as the last child it covers the table.

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

    DetailOverlay built = build_detail_overlay(node, kind == ViewKind::History);
    overlay_ = built.panel;
    overlay_title_ = built.title;
    overlay_list_ = built.list;
    overlay_graph_ = built.graph;
    overlay_close_ = built.close;
    connect_clicked(overlay_close_, &Monitor::on_button_overlay_close);

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

  // Refills the open view from the data. Called on open and whenever the snapshot
  // changes.
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
      std::vector<std::string> items = build_process_tree_lines(processes_);
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
      const std::deque<double>* series = history_.series(detail_pid_);
      if (series != nullptr) {
        overlay_graph_.get()->set_values(
            std::vector<double>(series->begin(), series->end()));
      } else {
        overlay_graph_.get()->set_values({});
      }
    }
  }

  std::string thread_line(const ThreadInfo& thread) const {
    return pad(std::to_string(thread.tid), 7) + " " + process_state_code(thread.state) +
           "  " + format_cpu_time(thread.cpu_time_ns) + "  " + thread.name;
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

  // The confirmation popup's overlay and whether it is a force (vs a plain
  // terminate).
  guayaba::NodeRef<guayaba::PanelContainer> confirm_overlay_;
  bool confirm_force_ = false;

  HistoryStore history_;

  SystemInfo system_;
  // The full snapshot, filtered and sorted into `visible_` only when something
  // that changes the view does.
  std::vector<ProcessInfo> processes_;
  std::vector<ProcessInfo> visible_;
  bool dirty_ = true;
  // Set on every snapshot so an open detail view refills.
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

  double message_timer_ = 0.0;
  // PID awaiting a confirmation; 0 when no prompt is up.
  int confirm_pid_ = 0;

  // The search bar's resting hint, shown while the field is empty.
  static constexpr const char* kSearchHint = "name or user\u2026";
  std::string help_text_ =
      "[/] Search  [s] Sort  [i] Int  [\u2191\u2193] Move  [p] Nice  [t] Thr  [r] Tree  [g] Hist  "
      "[k] Kill  [K] Force  [z] Stop  [c] Cont  [q] Quit";
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_MONITOR_H
