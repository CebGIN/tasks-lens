#include <memory>

#include <guayaba/Guayaba.hpp>

using namespace guayaba;

namespace {

// Skeleton scene. The process monitor will replace the placeholders with:
//   top    summary bar (totals, load average, memory)
//   middle process table / tree / threads
//   bottom help and input line
// Hotkeys arrive once the engine gains keyboard input.
std::unique_ptr<Node> build_scene() {
    auto root = std::make_unique<Node>("root");

    NodeRef<PanelContainer> panel = root->add_child<PanelContainer>("panel", Vec2{0, 0});
    panel->set_anchor_preset(AnchorPreset::FullRect);
    panel->set_clip_children(true);
    panel->get_stylebox().set_content_margin(1, 1, 1, 1);

    NodeRef<VBox> layout = panel->add_child<VBox>("layout", Vec2{0, 0});
    layout->set_anchor_preset(AnchorPreset::FullRect);

    NodeRef<Label> title = layout->add_child<Label>("title");
    title->set_text({"tasks-lens"});
    title->set_fit_content(true);

    NodeRef<Label> summary = layout->add_child<Label>("summary");
    summary->set_text({"summary: (pending /proc reader)"});
    summary->set_fit_content(true);

    NodeRef<Label> table = layout->add_child<Label>("table");
    table->set_text({"process table: (pending)"});
    table->set_fit_content(true);

    NodeRef<Label> help = layout->add_child<Label>("help");
    help->set_text({"q: quit  (hotkeys pending keyboard input)"});
    help->set_fit_content(true);

    return root;
}

} // namespace

int main() {
    SceneManager::get_instance().change_scene(build_scene());
    SceneManager::get_instance().start_running();
    return 0;
}
