#ifndef TASKS_LENS_SCRIPTS_DETAIL_OVERLAY_H
#define TASKS_LENS_SCRIPTS_DETAIL_OVERLAY_H

#include <guayaba/Guayaba.hpp>

#include "Scripts/ui_style.h"

namespace tasks_lens {

// A full-screen detail view added under `parent`: a titled header with a Close
// button, plus a ListView (threads/tree) or a Graph (history). The caller wires
// Close, sets the title, and owns the panel node.
struct DetailOverlay {
  guayaba::NodeRef<guayaba::PanelContainer> panel;
  guayaba::NodeRef<guayaba::Label> title;
  guayaba::NodeRef<guayaba::ListView> list;   // null in the history view
  guayaba::NodeRef<guayaba::Graph> graph;     // null in the list views
  guayaba::NodeRef<guayaba::Button> close;
};

inline DetailOverlay build_detail_overlay(guayaba::Node* parent, bool history) {
  guayaba::NodeRef<guayaba::PanelContainer> panel =
      parent->add_child<guayaba::PanelContainer>("overlay");
  panel->set_anchor_preset(guayaba::AnchorPreset::FullRect);
  panel->set_clip_children(true);
  panel->get_stylebox().set_background_color(guayaba::Color::BLACK);
  panel->get_stylebox().set_border_style(guayaba::StyleBorder::DOUBLE);
  panel->get_stylebox().set_border_width(1);
  panel->get_stylebox().set_border_color(guayaba::Color::BRIGHT_CYAN);
  panel->get_stylebox().set_content_margin(0, 0, 0, 0);

  guayaba::NodeRef<guayaba::VBox> box =
      panel->add_child<guayaba::VBox>("overlay_layout");
  box->set_anchor_preset(guayaba::AnchorPreset::FullRect);
  box->set_content_alignment(guayaba::ContentAlignment::Begin);
  box->set_separation(1);

  guayaba::NodeRef<guayaba::HBox> header =
      box->add_child<guayaba::HBox>("overlay_header");
  header->set_content_alignment(guayaba::ContentAlignment::Begin);
  header->set_separation(1);
  // Fill the overlay's width so the title grows and pushes Close to the edge.
  header->set_anchor_right(1.0f);
  header->set_grow_x(guayaba::Grow::Both);

  guayaba::NodeRef<guayaba::Label> title =
      header->add_child<guayaba::Label>("overlay_title");
  title->set_fit_content(true);
  title->set_text_color(guayaba::Color::BRIGHT_CYAN);
  title->set_anchor_right(1.0f);
  title->set_grow_x(guayaba::Grow::Both);

  guayaba::NodeRef<guayaba::Button> close =
      add_tool_button(header, "overlay_close", "Close");

  guayaba::NodeRef<guayaba::ListView> list;
  guayaba::NodeRef<guayaba::Graph> graph;
  if (history) {
    graph = box->add_child<guayaba::Graph>("overlay_graph");
    graph->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    graph->set_grow_y(guayaba::Grow::Both);
    graph->set_line_color(guayaba::Color::BRIGHT_GREEN);
  } else {
    list = box->add_child<guayaba::ListView>("overlay_list");
    list->set_anchor_preset(guayaba::AnchorPreset::FullRect);
    list->set_grow_y(guayaba::Grow::Both);
    list->set_text_color(guayaba::Color::WHITE);
    list->grab_focus();
  }

  DetailOverlay overlay;
  overlay.panel = panel;
  overlay.title = title;
  overlay.list = list;
  overlay.graph = graph;
  overlay.close = close;
  return overlay;
}

}  // namespace tasks_lens

#endif  // TASKS_LENS_SCRIPTS_DETAIL_OVERLAY_H
