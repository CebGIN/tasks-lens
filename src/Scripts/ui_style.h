#ifndef TASKS_LENS_SCRIPTS_UI_STYLE_H
#define TASKS_LENS_SCRIPTS_UI_STYLE_H

#include <string>

#include <guayaba/Guayaba.hpp>

namespace tasks_lens {

// One look for every action button in the app: a blue band at rest, bright blue
// under the pointer, inverted while held, and a flat black "off" state for an
// action that has no target (no selected row). Kept in one place so the toolbar,
// the overlay Close and the confirmation Yes/No cannot drift apart.
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
  // Disabled is the only state whose text colour the default does not already
  // set, so it goes through the name overload (there is no BRIGHT_BLACK constant).
  btn->set_text_color(guayaba::ButtonState::Disabled, "BRIGHT_BLACK");
}

// Creates a button in `parent` with the toolbar look, and hands back the
// reference so the caller can wire "clicked" or toggle it disabled.
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
