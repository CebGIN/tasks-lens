#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <guayaba/Guayaba.hpp>

#include "Data/cpu_sampler.h"
#include "Data/process_source.h"
#include "Scripts/format.h"
#include "Scripts/monitor.h"
#include "Scripts/table_controller.h"
#include "Scripts/ui_style.h"

using namespace guayaba;

namespace {

// Column layout for the process table. Header button text is padded to `width`,
// so a button's automatic minimum size is exactly the column width.
// `width` is the fixed column width, except for the one column with `fills`:
// that one starts at `width` (its minimum) and grows to use every column left
// over, so long command lines are not clipped at a fixed 16.
struct ColumnSpec {
  const char* key;
  const char* title;
  int width;
  bool fills;
};

constexpr ColumnSpec kColumns[] = {
    {"pid", "PID", 5, false},      {"ppid", "PPID", 6, false},
    {"user", "USER", 10, false},   {"state", "STATE", 6, false},
    {"cpu", "%CPU", 6, false},     {"mem", "%MEM", 6, false},
    {"res", "RES", 8, false},      {"thr", "THR", 5, false},
    {"pri", "PRI", 5, false},      {"time", "TIME+", 9, false},
    {"command", "COMMAND", 16, true},
};
constexpr int kColumnCount = sizeof(kColumns) / sizeof(kColumns[0]);

// Column header: the title is also the sort trigger. Per-state backgrounds
// make the header row read as a band. The Normal state (active or not) comes
// from tasks_lens::apply_header_normal_style so the controller stays the single
// source of truth for header appearance.
NodeRef<Button> create_header_button(const NodeRef<Container>& parent,
                                     const std::string& node_name,
                                     const std::string& label) {
  NodeRef<Button> btn = parent->add_child<Button>(node_name);
  btn->set_text(label);
  // Column widths come from custom_minimum_size, so the title is not padded; it
  // sits centred in whatever width the column is given.
  btn->set_text_align(TextAlign::Center);

  tasks_lens::apply_header_normal_style(btn.get(), false);

  StyleBox hover;
  hover.set_content_margin(0,1,0,1);
  hover.set_background_color(Color::BRIGHT_BLUE);
  btn->set_stylebox(ButtonState::Hover, hover);
  btn->set_text_color(ButtonState::Hover, Color::BRIGHT_YELLOW);

  StyleBox pressed;
  pressed.set_content_margin(0,1,0,1);
  pressed.set_background_color(Color::BRIGHT_WHITE);
  btn->set_stylebox(ButtonState::Pressed, pressed);
  btn->set_text_color(ButtonState::Pressed, Color::BLACK);

  StyleBox disabled;
  disabled.set_content_margin(0,1,0,1);
  disabled.set_background_color(Color::BLACK);
  btn->set_stylebox(ButtonState::Disabled, disabled);
  btn->set_text_color(ButtonState::Disabled, Color::BLACK);

  return btn;
}

// Process table: one bordered panel holding one VBox per column. Each column
// stacks the header button (title + sort trigger) over a nested VBox that the
// TableController fills with one row label per visible process.
struct TableParts {
  NodeRef<PanelContainer> table;
  std::vector<tasks_lens::TableController::Column> columns;
};

TableParts build_table(const NodeRef<Container>& parent) {
  NodeRef<PanelContainer> table = parent->add_child<PanelContainer>("table");
  // FullRect both stretches the panel across the layout row AND is what makes
  // Grow::Both meaningful: without anchors spanning 0..1 the grown size resolves
  // against a zero-size anchor rect and the table stays at its minimum.
  table->set_anchor_preset(AnchorPreset::FullRect);
  table->get_stylebox().set_background_color(Color::BLACK);
  table->get_stylebox().set_border_style(StyleBorder::DOUBLE);
  table->get_stylebox().set_border_width(1);
  table->get_stylebox().set_border_color(Color::BLUE);
  table->get_stylebox().set_content_margin(0, 0, 0, 0);

  NodeRef<HBox> columns = table->add_child<HBox>("table_columns");
  columns->set_anchor_preset(AnchorPreset::FullRect);
  columns->set_content_alignment(ContentAlignment::Begin);
  columns->set_separation(1);

  std::vector<tasks_lens::TableController::Column> specs;

  for (int c = 0; c < kColumnCount; ++c) {
    const ColumnSpec& col = kColumns[c];

    NodeRef<VBox> column_vbox =
        columns->add_child<VBox>(std::string("col_") + col.key);
    // FullRect makes the column fill the table's height. Horizontally it must NOT
    // grow (FullRect's grow_x is Both): the padding used to size a column is
    // trailing spaces, and the engine's measure_text strips those, so a column's
    // own minimum is only as wide as its longest text. custom_minimum_size fixes
    // the width regardless of text; the one filling column keeps grow_x = Both so
    // it, and only it, absorbs the leftover and reaches the right border.
    column_vbox->set_anchor_preset(AnchorPreset::FullRect);
    column_vbox->set_custom_minimum_size(Vec2{col.width, 0});
    if (!col.fills) column_vbox->set_grow_x(Grow::None);
    column_vbox->set_content_alignment(ContentAlignment::Begin);
    column_vbox->set_separation(1);

    NodeRef<Button> btn = create_header_button(
        column_vbox, std::string("sort_") + col.key, col.title);
    // Fill the column's width so the header band and the row cells reach its
    // right edge. grow_x only stretches against the anchor span, and the default
    // TopLeft anchors are a zero-width span, so the right anchor has to be pulled
    // out to 1 as well. Height is left alone (top == bottom == 0), so neither
    // becomes a vertical expander.
    btn->set_anchor_right(1.0f);
    btn->set_grow_x(Grow::Both);

    NodeRef<VBox> rows =
        column_vbox->add_child<VBox>(std::string("rows_") + col.key);
    rows->set_anchor_right(1.0f);
    rows->set_grow_x(Grow::Both);
    rows->set_content_alignment(ContentAlignment::Begin);

    specs.push_back({col.key, col.width, col.fills, col.width, rows, btn});
  }

  // The table is the one row allowed to grow: it absorbs the slack so the help
  // line stays pinned at the bottom. Clipping keeps overflowing rows from
  // painting over that line.
  table->set_grow_y(Grow::Both);
  table->set_clip_children(true);

  return {table, std::move(specs)};
}

// Headless verification path for the data layer: takes two snapshots a second
// apart so the CPU deltas are non-zero, then prints a top-like excerpt to
// stdout. No terminal takeover, so it can run in a pipe.
int run_dump() {
  std::unique_ptr<tasks_lens::ProcessSource> source =
      tasks_lens::create_system_process_source();
  tasks_lens::CpuSampler sampler;

  for (int pass = 0; pass < 2; ++pass) {
    tasks_lens::SystemInfo sys = source->read_system();
    std::vector<tasks_lens::ProcessInfo> procs = source->read_processes();
    sampler.apply(sys, procs);

    std::printf("=== %s pass %d ===\n", source->platform_name(), pass + 1);
    std::printf(
        "tasks %d total, %d running, %d stopped, %d zombie | load %.2f %.2f %.2f\n",
        sys.total, sys.running, sys.stopped, sys.zombie, sys.load_avg[0],
        sys.load_avg[1], sys.load_avg[2]);
    std::printf("mem %.1f MiB total, %.1f MiB used, %.1f MiB avail | cpu %.1fs\n",
                sys.mem_total / 1048576.0, sys.mem_used / 1048576.0,
                sys.mem_available / 1048576.0, sys.total_cpu_time_ns / 1e9);

    std::sort(procs.begin(), procs.end(),
              [](const tasks_lens::ProcessInfo& a,
                 const tasks_lens::ProcessInfo& b) {
                return a.cpu_percent > b.cpu_percent;
              });

    const std::size_t shown = procs.size() < 10 ? procs.size() : 10;
    for (std::size_t i = 0; i < shown; ++i) {
      const tasks_lens::ProcessInfo& p = procs[i];
      std::printf("%7d %7d %-8s %s %5.1f %5.1f %8.1fM t%-3d n%-3d %8.2fs  %s\n",
                  p.pid, p.ppid, p.user.c_str(),
                  tasks_lens::process_state_code(p.state), p.cpu_percent,
                  p.mem_percent, p.rss_bytes / 1048576.0, p.thread_count,
                  p.priority, p.cpu_time_ns / 1e9, p.command.c_str());
    }
    if (pass == 0) std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  return 0;
}

// Full-screen scene: summary up top, the live process table in the middle, a
// status/hotkey line at the bottom. A TableController on the table owns sorting
// and the visible row window; a Monitor on the root owns the data and hotkeys.
std::unique_ptr<Node> build_scene() {
  auto root = std::make_unique<Node>("root");

  NodeRef<PanelContainer> panel =
      root->add_child<PanelContainer>("panel", Vec2{0, 0});
  panel->set_anchor_preset(AnchorPreset::FullRect);
  panel->set_clip_children(true);
  panel->get_stylebox().set_content_margin(0, 0, 0, 0);
  panel->get_stylebox().set_border_style(StyleBorder::DOUBLE);
  panel->get_stylebox().set_border_width(1);
  panel->get_stylebox().set_border_color(Color::BLUE);
  panel->get_stylebox().set_background_color(Color::BLACK);

  NodeRef<VBox> layout = panel->add_child<VBox>("layout", Vec2{0, 0});
  layout->set_anchor_preset(AnchorPreset::FullRect);
  layout->set_content_alignment(ContentAlignment::Begin);

  // 1. Title row: the title on the left, the global actions on the right. The
  //    title grows into the free space so the buttons land against the right
  //    edge (the same anchor + grow trick the table headers use inside a column).
  NodeRef<HBox> title_row = layout->add_child<HBox>("title_row");
  title_row->set_content_alignment(ContentAlignment::Begin);
  title_row->set_separation(1);
  // Fill the row's width (the cross axis of the layout VBox) so its children have
  // leftover space to grow into; without this the row is only as wide as its
  // children and nothing can be pushed to the right edge.
  title_row->set_anchor_right(1.0f);
  title_row->set_grow_x(Grow::Both);

  NodeRef<Label> title = title_row->add_child<Label>("title");
  title->set_text({"tasks-lens — Process Monitor"});
  title->set_text_color(Color::BRIGHT_CYAN);
  title->set_fit_content(true);
  title->set_anchor_right(1.0f);
  title->set_grow_x(Grow::Both);

  NodeRef<Button> btn_sort = tasks_lens::add_tool_button(title_row, "btn_sort", "Sort");
  NodeRef<Button> btn_interval =
      tasks_lens::add_tool_button(title_row, "btn_interval", "Interval");
  NodeRef<Button> btn_quit = tasks_lens::add_tool_button(title_row, "btn_quit", "Quit");

  // 2. Search bar: the name/user filter, always visible at the top. The LineEdit
  //    takes the focus itself on click, so this is the mouse path into filtering.
  NodeRef<HBox> search_row = layout->add_child<HBox>("search_row");
  search_row->set_content_alignment(ContentAlignment::Begin);
  search_row->set_separation(1);
  search_row->set_anchor_right(1.0f);
  search_row->set_grow_x(Grow::Both);

  NodeRef<Label> search_label = search_row->add_child<Label>("search_label");
  search_label->set_text({"Search:"});
  search_label->set_text_color(Color::BRIGHT_WHITE);
  search_label->set_fit_content(true);

  NodeRef<LineEdit> prompt = search_row->add_child<LineEdit>("prompt");
  prompt->set_anchor_right(1.0f);
  prompt->set_grow_x(Grow::Both);
  prompt->set_placeholder("name or user\u2026");
  prompt->set_text_color(Color::BRIGHT_WHITE);
  prompt->set_placeholder_color("BRIGHT_BLACK");
  StyleBox prompt_box;
  prompt_box.set_content_margin(1, 0, 1, 0);
  prompt_box.set_background_color(Color::BLACK);
  prompt->set_stylebox(prompt_box);

  NodeRef<Button> btn_clear = tasks_lens::add_tool_button(search_row, "btn_clear", "Clear");

  // 3. System summary (filled by the Monitor each refresh)
  NodeRef<Label> summary_tasks = layout->add_child<Label>("summary_tasks");
  summary_tasks->set_text({"Tasks: loading..."});
  summary_tasks->set_text_color(Color::WHITE);
  summary_tasks->set_fit_content(true);

  NodeRef<Label> summary_mem = layout->add_child<Label>("summary_mem");
  summary_mem->set_text({"CPU: ... | Mem: ... | Load: ..."});
  summary_mem->set_text_color(Color::WHITE);
  summary_mem->set_fit_content(true);

  // 4. Action row: what is selected on the left, the process actions on the
  //    right. The selection label grows so the buttons hug the right edge.
  NodeRef<HBox> action_row = layout->add_child<HBox>("action_row");
  action_row->set_content_alignment(ContentAlignment::Begin);
  action_row->set_separation(1);
  action_row->set_anchor_right(1.0f);
  action_row->set_grow_x(Grow::Both);

  NodeRef<Label> selection = action_row->add_child<Label>("selection");
  selection->set_text({"Selected: -"});
  selection->set_text_color(Color::BRIGHT_GREEN);
  selection->set_fit_content(true);
  selection->set_anchor_right(1.0f);
  selection->set_grow_x(Grow::Both);

  NodeRef<Button> btn_kill = tasks_lens::add_tool_button(action_row, "btn_kill", "Kill");
  NodeRef<Button> btn_force = tasks_lens::add_tool_button(action_row, "btn_force", "Force");
  NodeRef<Button> btn_stop = tasks_lens::add_tool_button(action_row, "btn_stop", "Stop");
  NodeRef<Button> btn_cont = tasks_lens::add_tool_button(action_row, "btn_cont", "Cont");
  NodeRef<Button> btn_nice = tasks_lens::add_tool_button(action_row, "btn_nice", "Nice");
  NodeRef<Button> btn_threads =
      tasks_lens::add_tool_button(action_row, "btn_threads", "Threads");
  NodeRef<Button> btn_tree = tasks_lens::add_tool_button(action_row, "btn_tree", "Tree");
  NodeRef<Button> btn_history =
      tasks_lens::add_tool_button(action_row, "btn_history", "History");

  // 5. Process table (headers double as sort buttons)
  TableParts parts = build_table(layout);

  // 6. Help / status line at the bottom
  NodeRef<Label> help = layout->add_child<Label>("help");
  help->set_text_color(Color::CYAN);
  help->set_fit_content(true);

  // The table owns sorting and the row window.
  NodeRef<PanelContainer> table = parts.table;
  auto controller_ref =
      table->attach_script<tasks_lens::TableController>(std::move(parts.columns));
  tasks_lens::TableController* controller = controller_ref.script();

  // The root owns the data and the hotkeys.
  std::unique_ptr<tasks_lens::ProcessSource> source =
      tasks_lens::create_system_process_source();

  tasks_lens::MonitorToolbar toolbar;
  toolbar.sort = btn_sort;
  toolbar.interval = btn_interval;
  toolbar.quit = btn_quit;
  toolbar.clear = btn_clear;
  toolbar.kill = btn_kill;
  toolbar.force = btn_force;
  toolbar.stop = btn_stop;
  toolbar.cont = btn_cont;
  toolbar.nice = btn_nice;
  toolbar.threads = btn_threads;
  toolbar.tree = btn_tree;
  toolbar.history = btn_history;

  auto monitor_ref = root->attach_script<tasks_lens::Monitor>(
      std::move(source), summary_tasks, summary_mem, selection, help, prompt,
      toolbar, table, controller);
  tasks_lens::Monitor* monitor = monitor_ref.script();
  if (monitor != nullptr) help->set_text(monitor->help_text());

  return root;
}

} // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--dump") return run_dump();
  }

  SceneManager::get_instance().set_target_fps(30);
  SceneManager::get_instance().change_scene(build_scene());
  SceneManager::get_instance().start_running();
  return 0;
}
