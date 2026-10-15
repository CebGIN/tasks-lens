# GuayabaTUIEngine & Project Specification Reference Notes

This document summarizes the internal mechanisms, API details, and project requirements looked up while developing the `tasks-lens` scene (summary bar, process table, hotkeys line).

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
  - **Fixed in engine v1.0.3-alpha** (tasks-lens pin): `Button::draw` now passes the current state's `StyleBox` background to `put_text` (0 when the state has none), mirroring `Label::draw` — banded buttons no longer punch holes in their fill.

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

---

## 7. Process Table Patterns (`src/main.cpp`, header-driven table)

* **Structure**: `PanelContainer "table"` (DOUBLE border) > `HBox "table_columns"` (FullRect, `ContentAlignment::Begin`, separation 0) > one `VBox "col_<key>"` (FullRect, Begin) per column with a header `Button` (title = sort trigger) over a nested `VBox` of row `Label`s, and an empty `PanelContainer "sep_<key>"` (ASCII border width 1, no child) after every column but the last — its 2-cell frame is exactly the divider, stretched full height by FullRect anchors.
* **Column alignment**: header button text and every cell are padded to the column width, so a Button's automatic minimum size (text extent + frame) is exactly the column width — no ragged columns, no `set_custom_minimum_size` needed.
* **Column panels**: `set_grow_y(Grow::Both)` so each bordered column stretches the full height of the table HBox (HBox cells span the full cross axis); the table panel itself is the `Grow::Both` child of the layout VBox, absorbing vertical slack so the help line stays bottom-pinned.
* **Header button StyleBoxes**: per-state backgrounds (Normal BLUE / active BRIGHT_BLUE, Hover BRIGHT_BLUE, Pressed BRIGHT_WHITE, Disabled BLACK) with matching text colors. Note this deliberately revises the §3 foreground-only advice: the header band is a StyleBox decoration by design, and the Pressed state needs BLACK text on BRIGHT_WHITE to stay readable.
* **Border styles**: `StyleBorder::{NONE, ASCII, DOUBLE, ROUNDED, HEAVY, DASHED}`; `has_border()` requires style != NONE, a color, and width > 0.

---

## 8. Process Data Layer (`src/Data/`)

Platform seam mirroring the engine's `IOSTerminal` pattern: one pure-virtual
`ProcessSource` interface, selected by `create_system_process_source()` in
`process_source_factory.cpp` with `#if defined(_WIN32)` / `#elif defined(__linux__)`.
Each backend `.cpp` is `#ifdef`-guarded; the consumer `CMakeLists.txt` globs
`src/*.cpp` and `list(REMOVE_ITEM)`s the foreign backend (engine convention).

* **Neutral types** (`process_info.h`): `ProcessInfo`, `ThreadInfo`, `SystemInfo`,
  `ProcessState`. Nothing above the interface sees a `/proc` line or a Win32 struct.
* **Normalized unit**: every backend converts cumulative CPU time to **nanoseconds**
  (Linux jiffies via `sysconf(_SC_CLK_TCK)`; Windows FILETIME 100 ns units ×100).
  That is what lets one `CpuSampler` do the math for both OSes.
* **`CpuSampler`** (`cpu_sampler.{h,cpp}`): owns the previous snapshot keyed by pid
  and fills `cpu_percent` = `Δproc_ns / Δsystem_ns × 100` (spec item 3) and
  `mem_percent` = `rss / mem_total × 100`. Backends leave both at 0; the first call
  is a baseline, so pass 1 reports 0% and pass 2 is live.
* **Linux backend**: `/proc/<pid>/stat` (parsed from after the LAST `)` because
  `comm` may contain spaces/parens), `status` (uid) + `getpwuid_r` cache, `cmdline`
  (NUL→space), `stat`, `meminfo`, `loadavg`, `task/*` for threads. Actions via
  `kill`/`SIGTERM`/`SIGKILL`/`SIGSTOP`/`SIGCONT`/`setpriority`.
* **Windows backend** (read-only for now): Toolhelp `CreateToolhelp32Snapshot`
  (`SNAPPROCESS`/`SNAPTHREAD`), `GetProcessTimes`/`GetSystemTimes`,
  `GetProcessMemoryInfo` (PSAPI), `GlobalMemoryStatusEx`, `OpenProcessToken` for the
  user. Written but **not compiled anywhere yet**; actions and thread CPU time are
  stubbed. No load average / per-state counts exist on Windows (fields stay 0).
* **Verification**: `./build/tasks-lens --dump` prints two summaries a second apart
  plus a top-10 by CPU to stdout (no terminal takeover); the live table is driven
  by the same model. The interactive TUI needs a real tty (it can be exercised
  under a pseudo-terminal). The Windows path cannot be built on this machine.

---

## 9. Live TUI + Input (engine v1.1.x)

### Engine input API now used

- **Types**: `guayaba::Key` (ASCII-valued letters/digits, special keys ≥ 0x100),
  `guayaba::InputEvent` (`Type::KEY/MOUSE_BUTTON/MOUSE_MOTION/MOUSE_WHEEL`,
  `pressed`, per-event `ctrl/shift/alt`, `char32_t text`, `wheel_delta`).
- **`IOSTerminal`**: `take_input_events()`, `was_key_pressed(Key)`,
  `get_wheel_delta()`. `Input::drain_input_events()` returns the frame's events.
- **Routing**: `Node::route_input(std::vector<InputEvent>&)` walks children in
  reverse, then the node's script `on_input`; a control consumes an event by
  erasing it. `Control` adds `FocusMode`, `grab/release_focus`,
  `on_gui_input`, and the `gui_input` signal. Leftovers go to
  `Node::route_unhandled_input` and the `unhandled_input` bus signal.
- **Script hooks**: `Script::on_input(const InputEvent&) -> bool` (consumable,
  in the walk) and `on_unhandled_input(const InputEvent&)` (fallback).
- **Frame order**: `refresh_input → update_layout → route_input → process_scene →
  draw`. `on_process` therefore sees the current frame's resolved rects.
- **Pacing**: `SceneManager::set_target_fps(n)` (0 = uncapped); tasks-lens uses
  **30**. Without it the loop spins.

### Consumer scripts (`src/Scripts/`)

- **`table_controller.h`** — `Script<PanelContainer>` on the table. Owns sort
  state + header `clicked` wiring (`apply_header_normal_style`), and a
  **virtualized** row window: each column has a `NodeRef<Label>` pool sized to
  `capacity`. `render()` writes the slice `[offset, offset+capacity)` and colors
  rows (zombie `BRIGHT_RED`, disk-sleep `BRIGHT_YELLOW`). Default sort is
  **`pid` ascending** — CPU% (the "top" default) reshuffles several times a
  second because the deltas swing, which reads as flicker. `format.h` holds the
  cell/byte/time/percent formatting.
  - **Selection cursor**: the highlighted row is drawn by setting every cell
    `Label`'s `set_background_color(...)` to a BRIGHT_BLACK band (the text colour,
    including the per-state colours, is left alone so it stays readable). The
    cursor is stored by **PID**, not index, and resolved against the current order
    each `render()`, so a re-sort (sorting by %CPU) cannot slide it onto a
    different process. `render()` also scrolls the window so the cursor stays on
    screen — the cursor is the scroll anchor, so there is no independent scroll.
  - **Tie-break**: the sort comparator falls back to PID ascending when the active
    column ties (e.g. every `root` under USER), making it a total order. Without
    it `std::sort` (not stable) plus the arbitrary `readdir` order reshuffled the
    tied block every refresh.
  - **Capacity measurement (the self-inflation trap)**: capacity must come from
    the **tty size** (`guayaba::create_system_terminal()->get_screen_size({0,0}).y`,
    a fresh `TIOCGWINSZ` each call), not from any resolved rect. Every row label
    is a line in its column's `get_minimum_size()`, so the table — and the panel
    around it, sized by its child — resolve to *at least* the pool's height.
    `resolve_rect` floors to that minimum, so reading the rect back makes
    capacity a function of the previous capacity: the page grows but can never
    shrink. The chrome between the tty edge and the rows is computed as
    `panel border (2) + (parent_minimum − table_minimum)` (the latter is the rows
    above/below plus gaps, pool-independent), then
    `− table border (2) − header − gap (1)`.
  - **Column widths (padding does not size a column)**: `TextWrap::measure_text`
    collapses whitespace runs and **strips trailing spaces**, so a header/cell
    padded with `pad(text, width)` reports a minimum of just the text length —
    padding never sets a width. Columns are therefore pinned with
    `Control::set_custom_minimum_size({width, 0})` on the column `VBox`, and every
    column is `grow_x = None` **except COMMAND**, which is the sole horizontal
    expander and so absorbs all the leftover (`HBox` splits slack among
    `Grow::Both` children on its main axis — with all 11 `FullRect` columns
    growing, the slack was spread evenly instead of reaching COMMAND). The
    expander's width is `screen_cols − 4` (outer panel + table borders) `− gaps
    (10) − fixed widths (66)`, clamped to a minimum; `render()` pads/truncates
    the COMMAND cell to exactly that. Header buttons and row `VBox`es also set
    `anchor_right(1)` + `grow_x Both` so they fill the column (grow only stretches
    against the anchor span, and the default `TopLeft` span is zero-width); the
    header buttons use no horizontal `content_margin`, so a title cannot inflate
    its column's minimum past the width the column was given. Header titles are
    centred with `Button::set_text_align(TextAlign::Center)` (engine ≥ v1.1.2-alpha;
    Left/Center/Right, moves the block without changing wrap or minimum). Row
    cells stay left-aligned.
- **`monitor.h`** — `Script<Node>` on the **root**. Owns `ProcessSource`,
  `CpuSampler`, `SystemInfo`, the snapshot, and the summary/selection/status
  labels. Every frame: sort by the table's sort state → `table->render(procs)`
  → mirror the cursor into the `Selected:` label. Every **interval** (default
  1 s, `i` cycles 1/2/5/10): re-read `/proc` + sample + update summary. Runs
  after controls, so it only sees keys nothing else consumed.
  - **Navigation** (selection): `↑/↓` move the cursor one row, `PgUp/PgDn` a page,
    `Home/End` to the ends, the wheel moves it a few rows, and a left click on a
    data row selects it (a header click is consumed by its sort `Button` first).
  - **Actions on the cursor** (spec items 9/14/15): `k` → SIGTERM, `Shift+K` →
    SIGKILL behind a separate `[y/N]` confirmation, `z` → SIGSTOP, `c` →
    SIGCONT. `Shift+K` is detected via `event.text == U'K'` because the Linux
    backend reports printable keys with `shift=false` (the shifted character is
    already in `text`). A successful action sets `elapsed_ = interval_` so the
    next `on_process` re-reads `/proc` immediately (the row disappears / the STATE
    updates); a failure shows `PID N: failed (gone or no permission)` — the
    backend returns one `bool`, so the two cases read the same to the user.
    While a confirmation is pending its keys are swallowed and the status line
    is not auto-cleared.
  - **Input line** (`prompt`, an engine `LineEdit`, spec 10/13): `f` focuses it
    as a live case-insensitive name/user filter; `p` focuses it as a priority
    (nice) editor, left empty with the current value shown in the prompt.
    `Enter` submits, `Escape` cancels. While focused it consumes typing, so the
    hotkeys stay quiet and only `Escape` reaches the root script. The filter
    drives `visible_` (the snapshot filtered + sorted); `processes_` stays the
    full snapshot, and the table, cursor and actions all read `visible_`.
  - **Detail views** (spec 5/6/7): `t` threads, `r` process tree, `g` CPU history.
    The engine has no node visibility, so each is a full-screen overlay
    `PanelContainer` built on demand, added to the root (draws on top, routes
    input first) and `queue_free()`d on `Escape`. Threads/tree use an engine
    `ListView`; history uses an engine `Graph` fed by `history_`, a capped CPU%
    ring per pid keyed by pid so it survives the table's re-sorting.
- `SystemInfo.idle_cpu_time_ns` (Linux idle+iowait, Windows idle) is what lets
  `Monitor` show overall `CPU: NN.N%` from `(Δtotal − Δidle)/Δtotal`.

### Layout gotcha rediscovered

A `PanelContainer` (or any `Control`) that is a `Grow::Both` child **must** also
have `set_anchor_preset(AnchorPreset::FullRect)`. With default `TopLeft` anchors
the grown size resolves against a zero-size anchor rect and the control stays at
its minimum — this was why the table was one row tall until the preset was set.

### Build note

`cmake --build build` now works: the submodule is re-pinned to the engine tag
with the input API (v1.1.1-alpha). Earlier in Phase C it was verified by
compiling `src/` directly against the engine working copy, before the tag existed.

---

## 10. New engine controls (repo `GuayabaTUIEngine`, pending pin)

The app consumes the engine through the **`external/GuayabaTUIEngine` submodule**,
pinned to a tag. New generic controls are built in the engine's own working copy
(`/home/cebrian/Documentos/Code/GuayabaTUIEngine`) and become usable here only
after the engine is committed/tagged/pushed and the pin advances
(`git -C external/GuayabaTUIEngine checkout <tag>` then
`git add external/GuayabaTUIEngine`). Until then, features that need them are
blocked.

### `LineEdit` (built, verified in the engine)

Single-line text field: `guayaba::LineEdit : Control`, in
`include/guayaba/Nodes/UI/LineEdit.hpp`.

- Text: `set_text` / `get_text` / `clear`. Placeholder: `set_placeholder` /
  `get_placeholder`.
- Editing: `set_max_length` (counts codepoints; ≤0 is unlimited), `get_caret` /
  `set_caret` (byte offset into the UTF-8 text, always snapped to a codepoint
  boundary).
- Echo: `set_secret(bool)`, `set_secret_character(char32_t)` (rendering masks the
  text; `get_text()` still returns it).
- Appearance: `get_stylebox` / `set_stylebox`, `set_text_color`,
  `set_placeholder_color`, `set_background_color`.
- Signals: `"text_changed"(std::string)` (fires only when the value really
  changes), `"text_submitted"(std::string)` (on ENTER).
- Behavior: `FocusMode::CLICK`; it takes focus on a left click **itself** — the
  engine does not auto-focus on click, and `Tab`/`Escape` are declined so the
  scene keeps them (the app will use Escape to cancel/defocus). Single line; the
  caret cell is drawn reverse-video and the text scrolls horizontally to keep it
  visible.

Intended app use: the name filter (spec 13) and the priority editor (spec 10) —
focus it with a hotkey, read `text_submitted`/`text_changed`, unfocus with Escape.

### `Graph` (built, verified in the engine)

Plot/sparkline: `guayaba::Graph : Control`, in
`include/guayaba/Nodes/UI/Graph.hpp`. No StyleBox — it draws edge to edge.

- Data: `set_values` (oldest→newest), `push_value`, `set_capacity`/`get_capacity`
  (a rolling window; `push_value` drops the oldest past capacity).
- Range: auto by default (min/max of the visible slice, widened to a ±0.5 band
  when flat); `set_range(min,max)` / `clear_range()` to pin it (CPU wants 0..100).
- `set_fill(bool)` — filled bars (default) or a one-cell-per-column sparkline;
  `set_line_color`, `set_background_color`.
- Right-aligned, newest at the right edge. One row is the degenerate sparkline;
  H rows gives H×8 sub-cell levels via the `▁▂▃▄▅▆▇█` ladder.
- Intended app use: per-process CPU history (spec 7) — the app owns the ring
  buffer, `Graph` renders it.

### `ListView` (built, verified in the engine)

Selectable vertical list: `guayaba::ListView : Control`, in
`include/guayaba/Nodes/UI/ListView.hpp`.

- Items: `set_items(std::vector<std::string>)`, `get_items`, `item_count`.
- Selection: `selected()` (-1 = none), `set_selected`, `select_first/last`,
  `move_selection`; the cursor index is clamped and the window follows.
- Scroll window: `scroll()`, `set_scroll`, `visible_count()` (virtual contents —
  a thousand rows cost the same to draw as ten).
- Appearance: `get_stylebox`/`set_stylebox`, `set_text_color`,
  `set_selection_background_color` (default BRIGHT_BLACK), `set_selection_text_color`,
  and a sparse `set_item_color(index, color)` for per-row tints.
- Signals: `"selection_changed"(int)` (only when it really moves),
  `"item_activated"(int)` (ENTER or a second click).
- `FocusMode::CLICK`; takes focus and selects on click; keys UP/DOWN/PgUp/PgDn/Home/End;
  wheel scrolls the window; declines TAB/ESCAPE.
- Intended app use: threads of a process (spec 6) and — with the app flattening
  the hierarchy and indenting the strings — the process tree (spec 5).

### Not planned

- A `TreeView`: the spec asks for a parent→child *view*, not collapse/expand. A
  flattened, indented `ListView` covers it, so a dedicated tree control would be
  scope the spec does not require.
- A modal `Dialog`: the force-kill confirmation is already handled on the bottom
  status line (`[y/N]`), so a modal is not needed for spec 14.


