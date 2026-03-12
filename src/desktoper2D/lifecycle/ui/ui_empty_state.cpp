#include "desktoper2D/lifecycle/ui/ui_empty_state.h"

#include "imgui.h"

namespace desktoper2D {

void RenderUnifiedEmptyState(const char *child_id,
                             const char *title,
                             const char *detail,
                             const ImVec4 &accent) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.96f, 0.97f, 0.99f, 1.0f));
    ImGui::BeginChild(child_id, ImVec2(-1.0f, 76.0f), ImGuiChildFlags_Borders);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::TextColored(accent, "%s", title != nullptr ? title : "");
    if (detail != nullptr && detail[0] != '\0') {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", detail);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace desktoper2D
