#ifndef TASKS_LENS_SCRIPTS_CONFIRM_DIALOG_H
#define TASKS_LENS_SCRIPTS_CONFIRM_DIALOG_H

#include <string>

#include <guayaba/Guayaba.hpp>

#include "Scripts/ui_style.h"

namespace tasks_lens {

// A full-screen modal confirmation added under `parent`: a bordered box holding
// `message` over Yes and No buttons. The caller wires the buttons and owns the
// overlay node.
struct ConfirmDialog {
  guayaba::NodeRef<guayaba::PanelContainer> overlay;
  guayaba::NodeRef<guayaba::Button> yes;
  guayaba::NodeRef<guayaba::Button> no;
};

inline ConfirmDialog build_confirm_dialog(guayaba::Node* parent,
                                          const std::string& message) {
  guayaba::NodeRef<guayaba::PanelContainer> overlay =
      parent->add_child<guayaba::PanelContainer>("confirm");
  overlay->set_anchor_preset(guayaba::AnchorPreset::FullRect);
  overlay->set_clip_children(true);
  overlay->get_stylebox().set_background_color(guayaba::Color::BLACK);
  overlay->get_stylebox().set_content_margin(0, 0, 0, 0);

  // VBox(Center) inside HBox(Center) centres the box on both axes: a container
  // centres its block along its own main axis.
  guayaba::NodeRef<guayaba::VBox> outer =
      overlay->add_child<guayaba::VBox>("confirm_outer");
  outer->set_anchor_preset(guayaba::AnchorPreset::FullRect);
  outer->set_content_alignment(guayaba::ContentAlignment::Center);

  guayaba::NodeRef<guayaba::HBox> middle =
      outer->add_child<guayaba::HBox>("confirm_middle");
  middle->set_content_alignment(guayaba::ContentAlignment::Center);

  guayaba::NodeRef<guayaba::PanelContainer> box =
      middle->add_child<guayaba::PanelContainer>("confirm_box");
  box->set_custom_minimum_size(guayaba::Vec2{54, 0});
  box->get_stylebox().set_background_color(guayaba::Color::BLACK);
  box->get_stylebox().set_border_style(guayaba::StyleBorder::DOUBLE);
  box->get_stylebox().set_border_width(1);
  box->get_stylebox().set_border_color(guayaba::Color::BRIGHT_RED);
  box->get_stylebox().set_content_margin(1, 0, 1, 0);

  guayaba::NodeRef<guayaba::VBox> inner =
      box->add_child<guayaba::VBox>("confirm_inner");
  inner->set_content_alignment(guayaba::ContentAlignment::Center);
  inner->set_separation(1);

  guayaba::NodeRef<guayaba::Label> text =
      inner->add_child<guayaba::Label>("confirm_message");
  text->set_fit_content(true);
  text->set_text_color(guayaba::Color::BRIGHT_WHITE);
  text->set_text(message);

  guayaba::NodeRef<guayaba::HBox> buttons =
      inner->add_child<guayaba::HBox>("confirm_buttons");
  buttons->set_content_alignment(guayaba::ContentAlignment::Center);
  buttons->set_separation(2);

  ConfirmDialog dialog;
  dialog.overlay = overlay;
  dialog.yes = add_tool_button(buttons, "confirm_yes", "Yes");
  dialog.no = add_tool_button(buttons, "confirm_no", "No");
  return dialog;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_CONFIRM_DIALOG_H
