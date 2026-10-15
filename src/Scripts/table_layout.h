#ifndef TASKS_LENS_SCRIPTS_TABLE_LAYOUT_H
#define TASKS_LENS_SCRIPTS_TABLE_LAYOUT_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace tasks_lens {

// How many row labels fit this frame, and how wide the filling column should be.
struct TableLayout {
  int capacity;
  int flex_width;
  bool has_flex;  // false when there is no filling column or no screen width
};

// `screen_rows`/`screen_cols` are the tty size in cells (0 when unknown);
// `chrome` is every row between the screen edge and the table's rows; `header`
// is the header band height. `widths`/`min_widths` are per column, in order;
// `flex_index` is the single filling column, or -1.
inline TableLayout compute_table_layout(int screen_rows, int screen_cols,
                                        int chrome, int header,
                                        const std::vector<int>& widths,
                                        const std::vector<int>& min_widths,
                                        int flex_index) {
  int flex_width = 0;
  bool has_flex = false;
  if (flex_index >= 0 && screen_cols > 0) {
    int fixed = 0;
    for (std::size_t i = 0; i < widths.size(); ++i) {
      if (static_cast<int>(i) != flex_index) fixed += widths[i];
    }
    const int gaps = static_cast<int>(widths.size()) - 1;
    flex_width = std::max(min_widths[static_cast<std::size_t>(flex_index)],
                          screen_cols - 4 - gaps - fixed);
    has_flex = true;
  }

  // Minus the table's double border (2), the header band and the gap under it.
  const int rows_height = screen_rows - chrome - 2 - header - 1;
  return {std::max(1, rows_height), flex_width, has_flex};
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_TABLE_LAYOUT_H
