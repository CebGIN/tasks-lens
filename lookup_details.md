# GuayabaTUIEngine & Project Specification Reference Notes

This document summarizes the internal mechanisms, API details, and project requirements looked up while developing the `tasks-lens` skeleton scene and top bar.

---

## 1. Project Requirements (`process_monitor_project_description.md`)

* **Process Table Columns**:
  The specification requires displaying:
  1. `PID` (Process Identifier)
  2. `PPID` (Parent Process ID)
  3. `USER` (Owner)
  4. `STATE` (Process State: Running, Sleeping, Stopped, Zombie)
  5. `%CPU` (Calculated delta CPU percentage)
  6. `%MEM` (Memory usage percentage)
  7. `RES` (Resident memory)
  8. `THREADS` / `THR` (Thread count)
  9. `PRI` (Priority / Nice)
  10. `TIME+` (Accumulated CPU time)
  11. `COMMAND` / `CMD` (Executable name / command line)

* **Sorting**:
  - Primary sorting requirements: `%CPU` and `%MEM` in descending order (highest consumer first).
  - Additional sorting available for all table properties (PID, PPID, USER, RES, PRI, TIME+, etc.).

* **System Summary Header**:
  - Total process count, running, stopped, and zombie counts.
  - Load average (1, 5, 15 min).
  - Total, used, and free/available memory.

* **Layout Structure**:
  - Full-screen TUI adapting to window resize.
  - Top: Header with summary and sort controls.
  - Middle: Process table / tree / thread view.
  - Bottom: Status line and hotkey shortcuts.

---

## 2. GuayabaTUIEngine Architecture & UI API (`external/GuayabaTUIEngine`)

### Umbrella Header
* `#include <guayaba/Guayaba.hpp>` brings in the entire public UI API within the `guayaba` namespace.

### UI Hierarchy & Containers
* **`NodeRef<T>`**:
  - Handle used to reference engine nodes without owning them directly.
  - Child creation syntax: `parent->add_child<NodeType>("name", ...args)`.
* **`PanelContainer`**:
  - Stretches a single child across its interior content rect (area minus border and margins).
  - Margins set via `panel->get_stylebox().set_content_margin(left, top, right, bottom)`.
  - Anchors: `panel->set_anchor_preset(AnchorPreset::FullRect)` expands to fill parent.
  - `set_clip_children(true)` bounds rendering to panel boundaries.
* **`VBox` (Vertical Box)**:
  - Stacks children vertically.
  - Computes height of each child control using `get_minimum_size().y`.
  - Supports row separation: `set_separation(int)`.
* **`HBox` (Horizontal Box)**:
  - Arranges children side-by-side horizontally.
  - Computes width of each child using `get_minimum_size().x`.
  - Supports spacing between items: `set_separation(int)`.
* **`ContentAlignment` (since engine v1.0.1-alpha)**:
  - `enum class ContentAlignment { Begin, Center, End }`, set with
    `container->set_content_alignment(...)`; default is `Center`.
  - Decides where a `Container`/`VBox`/`HBox` puts its children's block along the
    main axis when the container is bigger than the block: `Begin` = start edge
    (top/left), `Center` = middle (the old behavior), `End` = end edge.
  - `VBox`/`HBox` cells now span the full cross axis of the resolved rect (a
    `VBox` cell is as wide as the box), so `Grow::Both`/`FullRect` children
    stretch edge to edge and `TopLeft`-anchored children sit at the box edge.
* **Cell expansion (since engine v1.0.1-alpha)**:
  - A Control child of a `VBox`/`HBox` with `Grow::Both` on the box's main axis
    (`set_grow_y(Grow::Both)` for `VBox`, `set_grow_x` for `HBox`) absorbs the
    leftover space: the cell grows by its share (`slack / n`, first `slack % n`
    expanders get one extra cell). The expanded block spans the whole box, so
    alignment has nothing left to offset and cannot overshoot the edge
    (v1.0.2-alpha fix).
  - Pattern for a full-screen monitor: header/sort rows hug their minimum, the
    table sets `Grow::Both`, the help line stays pinned at the bottom.

---

## 3. `Button` Component Details (`include/guayaba/Nodes/UI/Button.hpp`)

* **States & Precedence**:
  - `ButtonState::Disabled` > `ButtonState::Pressed` > `ButtonState::Hover` > `ButtonState::Normal`.
  - Fallback: Non-normal states fall back to `Normal` stylebox if unconfigured.

* **Sizing**:
  - Minimum size is calculated from `guayaba::measure_text(raw_text)` plus the `Normal` StyleBox frame size.
  - Compact 1-line buttons are achieved when `border_width` is 0 and content margins are 0.

* **Color & Style API**:
  - Text colors: `btn->set_text_color(ButtonState state, const std::string& color_name)` or `uint16_t`.
  - Default text colors: `Normal: 7 (White)`, `Hover: 7`, `Pressed: 7`, `Disabled: 8 (Bright Black)`.
  - Stylebox customization: `btn->get_stylebox(ButtonState state)`.

* **Rendering Specifics**:
  - In `Button::draw`, character background is currently drawn with `/*bg=*/0` (Black).
  - Therefore, using colored foreground text on clean terminal backgrounds (like brackets `[%CPU]`) produces the cleanest appearance without background clipping artifacts.

* **Signals & Interaction**:
  - Emits the `"clicked"` signal when `Input::l_click_just_pressed` occurs within button bounds.
  - Hover detection (`is_inside(Input::mouse_pos)`) runs automatically in `Button::update()`.

---

## 4. `Label` Component Details (`include/guayaba/Nodes/UI/Label.hpp`)

* **Text & Fitting**:
  - `label->set_text(std::string)`: Sets raw label content.
  - `label->set_fit_content(true)`: Causes the label to report its intrinsic text dimensions as minimum size, allowing parent `VBox`/`HBox` to correctly measure and allocate rows/columns.
* **Colors**:
  - `label->set_text_color(const std::string& color_name)`: Maps to standard terminal colors via `Color::from_name()`.

---

## 5. Color System (`include/guayaba/Core/Color.hpp`)

Available 4-bit standard and bright ANSI color constants:
* `Color::BLACK` (0), `Color::RED` (1), `Color::GREEN` (2), `Color::YELLOW` (3), `Color::BLUE` (4), `Color::MAGENTA` (5), `Color::CYAN` (6), `Color::WHITE` (7)
* `Color::BRIGHT_RED` (9), `Color::BRIGHT_GREEN` (10), `Color::BRIGHT_YELLOW` (11), `Color::BRIGHT_BLUE` (12), `Color::BRIGHT_MAGENTA` (13), `Color::BRIGHT_CYAN` (14), `Color::BRIGHT_WHITE` (15)

---

## 6. Lifecycle & Execution (`include/guayaba/Scene/SceneManager.hpp`)

* `SceneManager::get_instance().change_scene(std::unique_ptr<Node>)`: Replaces the active scene root.
* `SceneManager::get_instance().start_running()`: Starts the terminal event loop, enabling raw mode and mouse tracking (`\x1b[?1003h`, `\x1b[?1006h`), updating input state, running layout, and drawing frames.
