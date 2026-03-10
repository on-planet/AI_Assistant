#include "app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"

namespace desktoper2D {

void RenderRuntimeErrorPanel(AppRuntime &runtime) {
    static int error_filter_idx = 1; // 默认 Non-OK
    const char *filters[] = {"All", "Non-OK", "Failed", "Degraded"};
    error_filter_idx = std::clamp(error_filter_idx, 0, 3);

    ImGui::BeginChild("health_controls_child", ImVec2(-1.0f, 168.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Plugin Quick Control");
    if (ImGui::Button("Refresh Unified List")) {
        runtime.unified_plugin_refresh_requested = true;
        RefreshUnifiedPlugins(runtime);
    }
    if (!runtime.unified_plugin_scan_error.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Scan Error: %s", runtime.unified_plugin_scan_error.c_str());
    }

    if (ImGui::BeginListBox("##unified_plugin_quick_list", ImVec2(-1.0f, 70.0f))) {
        for (int i = 0; i < static_cast<int>(runtime.unified_plugin_entries.size()); ++i) {
            const auto &entry = runtime.unified_plugin_entries[static_cast<std::size_t>(i)];
            const bool selected = (i == runtime.unified_plugin_selected_index);
            ImGui::PushID(i);
            const std::string label = std::string(UnifiedPluginStatusLabel(entry.status)) + " " + entry.name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                runtime.unified_plugin_selected_index = i;
                SDL_strlcpy(runtime.unified_plugin_name_input, entry.id.c_str(), sizeof(runtime.unified_plugin_name_input));
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    const bool has_unified_selected = runtime.unified_plugin_selected_index >= 0 &&
                                      runtime.unified_plugin_selected_index < static_cast<int>(runtime.unified_plugin_entries.size());
    if (!has_unified_selected) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Switch Selected")) {
        if (has_unified_selected) {
            std::string err;
            const bool ok = SwitchUnifiedPluginById(runtime, runtime.unified_plugin_name_input, &err);
            if (ok) {
                runtime.unified_plugin_switch_status = "unified plugin switch ok";
                runtime.unified_plugin_switch_error.clear();
            } else {
                runtime.unified_plugin_switch_status.clear();
                runtime.unified_plugin_switch_error = err.empty() ? "unified plugin switch failed" : err;
            }
        }
    }
    if (!has_unified_selected) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    ImGui::InputTextWithHint("New Plugin", "新建插件名称", runtime.unified_plugin_new_name_input,
                             sizeof(runtime.unified_plugin_new_name_input));
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        std::string err;
        UserPluginCreateRequest req{};
        req.name = runtime.unified_plugin_new_name_input;
        const bool ok = CreateUserPlugin(runtime, req, &err);
        if (ok) {
            runtime.unified_plugin_create_status = "plugin created";
            runtime.unified_plugin_create_error.clear();
            runtime.unified_plugin_refresh_requested = true;
            RefreshUnifiedPlugins(runtime);
        } else {
            runtime.unified_plugin_create_status.clear();
            runtime.unified_plugin_create_error = err.empty() ? "plugin create failed" : err;
        }
    }

    if (!runtime.unified_plugin_switch_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.unified_plugin_switch_status.c_str());
    }
    if (!runtime.unified_plugin_switch_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.unified_plugin_switch_error.c_str());
    }
    if (!runtime.unified_plugin_create_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.unified_plugin_create_status.c_str());
    }
    if (!runtime.unified_plugin_create_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.unified_plugin_create_error.c_str());
    }

    ImGui::TextDisabled("Error Filter");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("##error_filter", &error_filter_idx, filters, 4);
    ImGui::EndChild();

    ImGui::BeginChild("health_primary_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    RenderUnifiedPluginStatusCard(runtime, "尚未发现 Unified Plugin 条目，请先在 Ops 面板刷新列表。");



    ImGui::SeparatorText("Runtime Error Classification");
    ImGui::BeginChild("error_table_child", ImVec2(-1.0f, 240.0f), ImGuiChildFlags_Borders);
    RenderRuntimeErrorClassificationTable(runtime, static_cast<ErrorViewFilter>(error_filter_idx));
    RenderRuntimeOpsActions(runtime);
    ImGui::EndChild();

    ImGui::EndChild();
}


}  // namespace desktoper2D
