#ifndef TASKS_LENS_SCRIPTS_UI_STYLE_H
#define TASKS_LENS_SCRIPTS_UI_STYLE_H

#include <string>

#include <guayaba/Guayaba.hpp>

namespace tasks_lens {

// Applies the shared toolbar look to `btn`: blue at rest, bright blue on hover,
// inverted while pressed, black when disabled. No-op when `btn` is null.
inline void apply_tool_button_style(guayaba::Button* btn) {
  if (btn == nullptr) return;

  guayaba::StyleBox normal;
  normal.set_content_margin(1, 0, 1, 0);
  normal.set_background_color(guayaba::Color::BLUE);
  btn->set_stylebox(guayaba::ButtonState::Normal, normal);
  btn->set_text_color(guayaba::ButtonState::Normal, guayaba::Color::CYAN);

  guayaba::StyleBox hover;
  hover.set_content_margin(1, 0, 1, 0);
  hover.set_background_color(guayaba::Color::BRIGHT_BLUE);
  btn->set_stylebox(guayaba::ButtonState::Hover, hover);
  btn->set_text_color(guayaba::ButtonState::Hover, guayaba::Color::BRIGHT_YELLOW);

  guayaba::StyleBox pressed;
  pressed.set_content_margin(1, 0, 1, 0);
  pressed.set_background_color(guayaba::Color::BRIGHT_WHITE);
  btn->set_stylebox(guayaba::ButtonState::Pressed, pressed);
  btn->set_text_color(guayaba::ButtonState::Pressed, guayaba::Color::BLACK);

  guayaba::StyleBox disabled;
  disabled.set_content_margin(1, 0, 1, 0);
  disabled.set_background_color(guayaba::Color::BLACK);
  btn->set_stylebox(guayaba::ButtonState::Disabled, disabled);
  // Disabled has no BRIGHT_BLACK constant, so the colour is set by name.
  btn->set_text_color(guayaba::ButtonState::Disabled, "BRIGHT_BLACK");
}

// Adds a button named `name` with text `label` to `parent`, applies the toolbar
// look, and returns it.
inline guayaba::NodeRef<guayaba::Button> add_tool_button(
    const guayaba::NodeRef<guayaba::Container>& parent, const std::string& name,
    const std::string& label) {
  guayaba::NodeRef<guayaba::Button> btn = parent->add_child<guayaba::Button>(name);
  btn->set_text(label);
  btn->set_text_align(guayaba::TextAlign::Center);
  apply_tool_button_style(btn.get());
  return btn;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_UI_STYLE_H
