#ifndef TASKS_LENS_SCRIPTS_VIEWPORT_H
#define TASKS_LENS_SCRIPTS_VIEWPORT_H

#include <memory>

#include <guayaba/Guayaba.hpp>
#include <guayaba/Terminal/IOSTerminal.hpp>

namespace tasks_lens {

// The tty handle used only to read the screen size. Created once, never init()ed,
// so it does not touch raw mode.
inline guayaba::IOSTerminal* system_terminal() {
  static std::unique_ptr<guayaba::IOSTerminal> terminal =
      guayaba::create_system_terminal();
  return terminal.get();
}

// Screen width in cells, or 0 when it cannot be read.
inline int terminal_cols() {
  guayaba::IOSTerminal* terminal = system_terminal();
  return terminal != nullptr ? terminal->get_screen_size({0, 0}).x : 0;
}

// Screen height in cells, or 0 when it cannot be read.
inline int terminal_rows() {
  guayaba::IOSTerminal* terminal = system_terminal();
  return terminal != nullptr ? terminal->get_screen_size({0, 0}).y : 0;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_VIEWPORT_H
