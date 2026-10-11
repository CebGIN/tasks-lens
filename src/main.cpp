#include <memory>

#include <guayaba/Guayaba.hpp>

using namespace guayaba;

namespace {

// Helper to construct interactive sort buttons for the top bar with consistent
// styling.
NodeRef<Button> create_sort_button(NodeRef<HBox> &parent,
                                   const std::string &node_name,
                                   const std::string &label,
                                   bool is_active = false) {
  NodeRef<Button> btn = parent->add_child<Button>(node_name);
  btn->set_text(label);

  // Active button stands out in Bright Green; others use Cyan.
  btn->set_text_color(ButtonState::Normal,
                      is_active ? Color::BRIGHT_GREEN : Color::CYAN);
  btn->set_text_color(ButtonState::Hover, Color::BRIGHT_YELLOW);
  btn->set_text_color(ButtonState::Pressed, Color::BRIGHT_WHITE);
  btn->set_text_color(ButtonState::Disabled, Color::BLACK);

  return btn;
}

// Skeleton scene. The process monitor layout adheres to the specification:
//   top    system summary bar (totals, load average, memory) and sort top bar
//   middle process table / tree / threads view
//   bottom hotkeys and action status line
// (Logic for /proc data querying and interactive sorting will connect to these
// nodes)
std::unique_ptr<Node> build_scene() {
  auto root = std::make_unique<Node>("root");

  NodeRef<PanelContainer> panel =
      root->add_child<PanelContainer>("panel", Vec2{0, 0});
  panel->set_anchor_preset(AnchorPreset::FullRect);
  panel->set_clip_children(true);
  panel->get_stylebox().set_content_margin(1, 1, 1, 1);

  NodeRef<VBox> layout = panel->add_child<VBox>("layout", Vec2{0, 0});
  layout->set_anchor_preset(AnchorPreset::FullRect);
  layout->set_content_alignment(ContentAlignment::Begin);

  // 1. Header & Title
  NodeRef<Label> title = layout->add_child<Label>("title");
  title->set_text({"tasks-lens — Process Monitor"});
  title->set_text_color(Color::BRIGHT_CYAN);
  title->set_fit_content(true);

  // 2. System summary placeholder (processes count, states, load average,
  // memory)
  NodeRef<Label> summary_tasks = layout->add_child<Label>("summary_tasks");
  summary_tasks->set_text(
      {"Tasks: 0 total, 0 running, 0 sleeping, 0 stopped, 0 zombie"});
  summary_tasks->set_text_color(Color::WHITE);
  summary_tasks->set_fit_content(true);

  NodeRef<Label> summary_mem = layout->add_child<Label>("summary_mem");
  summary_mem->set_text({"CPU: 0.0% usr, 0.0% sys | Mem: 0M total, 0M used, 0M "
                         "free | Load avg: 0.00 0.00 0.00"});
  summary_mem->set_text_color(Color::WHITE);
  summary_mem->set_fit_content(true);

  // 3. Full top bar with buttons for setting the property to sort by
  NodeRef<HBox> top_bar = layout->add_child<HBox>("top_bar");
  top_bar->set_separation(1);

  NodeRef<Label> sort_label = top_bar->add_child<Label>("sort_label");
  sort_label->set_text({"Sort by:"});
  sort_label->set_text_color(Color::BRIGHT_WHITE);
  sort_label->set_fit_content(true);

  // Sort buttons for process properties specified in the project requirements
  // Default sort by %CPU (active) as required by project spec
  create_sort_button(top_bar, "sort_cpu", "[%CPU]", /*is_active=*/true);
  create_sort_button(top_bar, "sort_mem", "[%MEM]");
  create_sort_button(top_bar, "sort_pid", "[PID]");
  create_sort_button(top_bar, "sort_ppid", "[PPID]");
  create_sort_button(top_bar, "sort_user", "[USER]");
  create_sort_button(top_bar, "sort_state", "[STATE]");
  create_sort_button(top_bar, "sort_res", "[RES]");
  create_sort_button(top_bar, "sort_threads", "[THR]");
  create_sort_button(top_bar, "sort_priority", "[PRI]");
  create_sort_button(top_bar, "sort_time", "[TIME+]");
  create_sort_button(top_bar, "sort_command", "[CMD]");

  // 4. Process table header & preview placeholder
  NodeRef<Label> table_header = layout->add_child<Label>("table_header");
  table_header->set_text({"  PID   PPID USER       STATE  %CPU  %MEM    RES  "
                          "THR  PRI     TIME+  COMMAND"});
  table_header->set_text_color(Color::BRIGHT_YELLOW);
  table_header->set_fit_content(true);

  NodeRef<Label> table = layout->add_child<Label>("table");
  table->set_text({"    1      0 root       S       0.0   0.1  12.4M    1   20 "
                   "  0:01.23  systemd\n"
                   "  450      1 root       S       0.0   0.0   4.2M    1   20 "
                   "  0:00.15  udevd\n"
                   " 1234      1 cebrian    R       2.5   1.2 180.4M    4   20 "
                   "  0:08.50  tasks-lens\n"
                   " ... (process table pending /proc reader)"});
  table->set_fit_content(true);
  // The table is the one row allowed to grow: it absorbs the slack so the help
  // line stays pinned at the bottom. Clipping keeps overflowing rows from
  // painting over that line.
  table->set_grow_y(Grow::Both);
  table->set_clip_children(true);

  // 5. Help / hotkeys line at bottom
  NodeRef<Label> help = layout->add_child<Label>("help");
  help->set_text({"[q] Quit  [s] Sort  [i] Interval  [f] Filter  [t] Tree  [h] "
                  "Threads  [k] Kill  [p] Priority"});
  help->set_text_color(Color::CYAN);
  help->set_fit_content(true);

  return root;
}

} // namespace

int main() {
  SceneManager::get_instance().change_scene(build_scene());
  SceneManager::get_instance().start_running();
  return 0;
}
