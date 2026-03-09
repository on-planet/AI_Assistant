#include "k2d/lifecycle/ui/app_debug_ui.h"

#include "k2d/lifecycle/ui/app_debug_ui_internal.h"

namespace k2d {

void RenderRuntimeErrorPanel(AppRuntime &runtime) {
    static int error_filter_idx = 1; // 默认 Non-OK
    const char *filters[] = {"All", "Non-OK", "Failed", "Degraded"};
    error_filter_idx = std::clamp(error_filter_idx, 0, 3);

    ImGui::BeginChild("error_filter_child", ImVec2(-1.0f, 54.0f), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Error Filter", &error_filter_idx, filters, 4);
    ImGui::EndChild();

    ImGui::BeginChild("error_table_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Runtime Error Classification");
    RenderRuntimeErrorClassificationTable(runtime, static_cast<ErrorViewFilter>(error_filter_idx));
    ImGui::EndChild();
}


}  // namespace k2d
