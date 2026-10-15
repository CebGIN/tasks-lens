#ifndef TASKS_LENS_SCRIPTS_MONITOR_H
#define TASKS_LENS_SCRIPTS_MONITOR_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
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
 public:
  Monitor(std::unique_ptr<ProcessSource> source,
          guayaba::NodeRef<guayaba::Label> summary_tasks,
          guayaba::NodeRef<guayaba::Label> summary_mem,
          guayaba::NodeRef<guayaba::Label> selection,
          guayaba::NodeRef<guayaba::Label> status,
          guayaba::NodeRef<guayaba::PanelContainer> table,
          TableController* table_controller)
      : source_(std::move(source)),
        summary_tasks_(summary_tasks),
        summary_mem_(summary_mem),
        selection_(selection),
        status_(status),
        table_(table),
        table_controller_(table_controller) {}

  void on_process(double delta_time) override {
    elapsed_ += delta_time;

    if (first_frame_ || elapsed_ >= interval_) {
      elapsed_ = 0.0;
      first_frame_ = false;
      refresh();
    }

    sort_processes();
    if (table_controller_ != nullptr) table_controller_->render(processes_);
    update_selection();

    // A pending confirmation owns the status line until it is answered, so the
    // prompt is not cleared out from under the user.
    if (confirm_pid_ == 0 && message_timer_ > 0.0) {
      message_timer_ -= delta_time;
      if (message_timer_ <= 0.0 && status_.get() != nullptr) {
        status_.get()->set_text(help_text_);
      }
    }
  }

  bool on_input(const guayaba::InputEvent& event) override {
    if (event.type == guayaba::InputEvent::Type::MOUSE_WHEEL) {
      if (table_.get() != nullptr && table_.get()->is_inside(event.mouse_pos)) {
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
      if (event.mouse_button == guayaba::MouseButton::LEFT && event.pressed &&
          table_.get() != nullptr && table_.get()->is_inside(event.mouse_pos) &&
          table_controller_ != nullptr) {
        return table_controller_->select_at(event.mouse_pos, processes_);
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
        if (table_controller_ != nullptr) table_controller_->select_to(0, processes_);
        return true;
      case guayaba::Key::END:
        if (table_controller_ != nullptr) {
          table_controller_->select_to(static_cast<int>(processes_.size()), processes_);
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
    update_summary();
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

  void sort_processes() {
    if (table_controller_ == nullptr) return;
    const std::string key = table_controller_->sort_key();
    const bool ascending = table_controller_->ascending();
    std::sort(processes_.begin(), processes_.end(),
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
      table_controller_->select_by(rows, processes_);
    }
  }

  // Mirrors the highlighted row into its own label, which is also where the
  // actions and the priority editor will read their target from.
  void update_selection() {
    if (selection_.get() == nullptr) return;
    std::string text = "Selected: -";
    if (table_controller_ != nullptr) {
      const int index = table_controller_->selected_index();
      if (index >= 0 && index < static_cast<int>(processes_.size())) {
        const ProcessInfo& proc = processes_[static_cast<std::size_t>(index)];
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
    if (index < 0 || index >= static_cast<int>(processes_.size())) return nullptr;
    return &processes_[static_cast<std::size_t>(index)];
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
  guayaba::NodeRef<guayaba::PanelContainer> table_;
  TableController* table_controller_ = nullptr;

  SystemInfo system_;
  std::vector<ProcessInfo> processes_;

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
      "[q] Quit  [s] Sort  [i] Interval  [\u2191/\u2193] Select  [k] Terminate  "
      "[K] Force  [z] Suspend  [c] Resume";
};

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_MONITOR_H
