#include "app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"

namespace desktoper2D {

void RenderRuntimePluginQuickControlPanel(AppRuntime &runtime) {
    RenderUnifiedPluginStatusCard(runtime, "尚未发现 Unified Plugin 条目，请先在 Ops 面板刷新列表。");
    if (ImGui::Button("Refresh Unified List")) {
        runtime.unified_plugin_refresh_requested = true;
        RefreshUnifiedPlugins(runtime);
    }
    if (!runtime.unified_plugin_scan_error.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Scan Error: %s", runtime.unified_plugin_scan_error.c_str());
    }

    if (ImGui::BeginListBox("##unified_plugin_quick_list", ImVec2(-1.0f, 140.0f))) {
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
    const bool can_delete_unified = has_unified_selected &&
                                    runtime.unified_plugin_entries[static_cast<std::size_t>(runtime.unified_plugin_selected_index)].kind == UnifiedPluginKind::BehaviorUser;
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
    if (!can_delete_unified) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Selected")) {
        std::string err;
        const bool ok = DeleteUnifiedPluginById(runtime, runtime.unified_plugin_name_input, &err);
        if (ok) {
            runtime.unified_plugin_delete_status = "plugin deleted";
            runtime.unified_plugin_delete_error.clear();
            runtime.unified_plugin_refresh_requested = true;
            RefreshUnifiedPlugins(runtime);
        } else {
            runtime.unified_plugin_delete_status.clear();
            runtime.unified_plugin_delete_error = err.empty() ? "plugin delete failed" : err;
        }
    }
    if (!can_delete_unified) {
        ImGui::EndDisabled();
    }

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
    if (!runtime.unified_plugin_delete_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.unified_plugin_delete_status.c_str());
    }
    if (!runtime.unified_plugin_delete_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.unified_plugin_delete_error.c_str());
    }

    ImGui::BeginChild("quick_asr_child", ImVec2(-1.0f, 220.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("ASR Provider");
    ImGui::Text("ASR Ready: %s", runtime.asr_ready ? "true" : "false");
    if (!runtime.asr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "ASR Error: %s", runtime.asr_last_error.c_str());
    }
    if (runtime.asr_provider_entries.empty()) {
        RefreshAsrProviders(runtime);
    }
    if (ImGui::BeginListBox("##asr_provider_list", ImVec2(-1.0f, 80.0f))) {
        for (int i = 0; i < static_cast<int>(runtime.asr_provider_entries.size()); ++i) {
            const auto &entry = runtime.asr_provider_entries[static_cast<std::size_t>(i)];
            const bool selected = (i == runtime.asr_selected_entry_index);
            ImGui::PushID(i);
            if (ImGui::Selectable(entry.name.c_str(), selected)) {
                runtime.asr_selected_entry_index = i;
                SDL_strlcpy(runtime.asr_provider_input, entry.name.c_str(), sizeof(runtime.asr_provider_input));
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::InputTextWithHint("ASR Provider", "offline/cloud/hybrid", runtime.asr_provider_input, sizeof(runtime.asr_provider_input));
    if (ImGui::Button("Switch ASR")) {
        std::string err;
        const bool ok = SwitchAsrProviderByName(runtime, runtime.asr_provider_input, &err);
        if (ok) {
            runtime.asr_switch_status = "asr switch ok";
            runtime.asr_switch_error.clear();
            runtime.asr_current_provider_name = runtime.asr_provider_input;
        } else {
            runtime.asr_switch_status.clear();
            runtime.asr_switch_error = err.empty() ? "asr switch failed" : err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Selected##asr")) {
        if (runtime.asr_selected_entry_index >= 0 &&
            runtime.asr_selected_entry_index < static_cast<int>(runtime.asr_provider_entries.size())) {
            const auto &entry = runtime.asr_provider_entries[static_cast<std::size_t>(runtime.asr_selected_entry_index)];
            SDL_strlcpy(runtime.asr_provider_input, entry.name.c_str(), sizeof(runtime.asr_provider_input));
        }
    }
    if (!runtime.asr_switch_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.asr_switch_status.c_str());
    }
    if (!runtime.asr_switch_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.asr_switch_error.c_str());
    }
    ImGui::EndChild();

    ImGui::BeginChild("quick_ocr_child", ImVec2(-1.0f, 220.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("OCR Model");
    ImGui::Text("OCR Ready: %s", runtime.perception_state.ocr_ready ? "true" : "false");
    if (!runtime.perception_state.ocr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "OCR Error: %s", runtime.perception_state.ocr_last_error.c_str());
    }
    if (runtime.ocr_model_entries.empty()) {
        RefreshOcrModels(runtime);
    }
    if (ImGui::BeginListBox("##ocr_model_list", ImVec2(-1.0f, 80.0f))) {
        for (int i = 0; i < static_cast<int>(runtime.ocr_model_entries.size()); ++i) {
            const auto &entry = runtime.ocr_model_entries[static_cast<std::size_t>(i)];
            const bool selected = (i == runtime.ocr_selected_entry_index);
            ImGui::PushID(i);
            if (ImGui::Selectable(entry.name.c_str(), selected)) {
                runtime.ocr_selected_entry_index = i;
                SDL_strlcpy(runtime.ocr_model_input, entry.name.c_str(), sizeof(runtime.ocr_model_input));
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::InputTextWithHint("OCR Model", "ppocr_v5_default", runtime.ocr_model_input, sizeof(runtime.ocr_model_input));
    if (ImGui::Button("Switch OCR")) {
        std::string err;
        const bool ok = SwitchOcrModelByName(runtime, runtime.ocr_model_input, &err);
        if (ok) {
            runtime.ocr_switch_status = "ocr switch ok";
            runtime.ocr_switch_error.clear();
        } else {
            runtime.ocr_switch_status.clear();
            runtime.ocr_switch_error = err.empty() ? "ocr switch failed" : err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Selected##ocr")) {
        if (runtime.ocr_selected_entry_index >= 0 &&
            runtime.ocr_selected_entry_index < static_cast<int>(runtime.ocr_model_entries.size())) {
            const auto &entry = runtime.ocr_model_entries[static_cast<std::size_t>(runtime.ocr_selected_entry_index)];
            SDL_strlcpy(runtime.ocr_model_input, entry.name.c_str(), sizeof(runtime.ocr_model_input));
        }
    }
    if (!runtime.ocr_switch_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.ocr_switch_status.c_str());
    }
    if (!runtime.ocr_switch_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.ocr_switch_error.c_str());
    }
    ImGui::EndChild();
}

void RenderRuntimeErrorPanel(AppRuntime &runtime) {
    static int error_filter_idx = 1; // 默认 Non-OK
    const char *filters[] = {"All", "Non-OK", "Failed", "Degraded"};
    error_filter_idx = std::clamp(error_filter_idx, 0, 3);

    ImGui::BeginChild("health_primary_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Runtime Error Classification");
    ImGui::BeginChild("error_table_child", ImVec2(-1.0f, 240.0f), ImGuiChildFlags_Borders);
    RenderRuntimeErrorClassificationTable(runtime, static_cast<ErrorViewFilter>(error_filter_idx));
    ImGui::EndChild();

    ImGui::SeparatorText("Runtime Ops");
    RenderRuntimeOpsActions(runtime);

    ImGui::EndChild();
}

}  // namespace desktoper2D
