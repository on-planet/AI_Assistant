#include "app_debug_ui_actions.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cfloat>
#include <functional>
#include <vector>

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_presenter.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"
#include "desktoper2D/lifecycle/ui/commands/ops_commands.h"
#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

namespace desktoper2D {

namespace {

const char *ToLogLevelLabel(PluginLogLevel level) {
    switch (level) {
        case PluginLogLevel::Info:
            return "info";
        case PluginLogLevel::Warning:
            return "warn";
        case PluginLogLevel::Error:
            return "error";
        default:
            return "unknown";
    }
}

}

void RenderRuntimeOpsActions(AppRuntime &runtime) {
    std::string &runtime_ops_status = RuntimeOpsStatusStorage();
    runtime_ops_status = runtime.runtime_ops_status;
    const UiCommandBridge bridge = BuildUiCommandBridge(runtime);
    OpsReadModel model = BuildOpsReadModel(runtime, runtime_ops_status);

    std::vector<RuntimeErrorRow> rows = BuildRuntimeErrorRows(runtime);
    std::string all_errors;
    all_errors.reserve(1024);
    for (const auto &row : rows) {
        all_errors += row.label;
        all_errors += " | count=";
        all_errors += std::to_string(static_cast<long long>(row.info->count));
        all_errors += " | degraded=";
        all_errors += std::to_string(static_cast<long long>(row.info->degraded_count));
        all_errors += " | recent_seq=";
        all_errors += std::to_string(row.recent_seq);
        all_errors += " | ";
        all_errors += RuntimeErrorDomainName(row.info->domain);
        all_errors += ".";
        all_errors += RuntimeErrorCodeName(row.info->code);
        if (!row.info->detail.empty()) {
            all_errors += " | detail=";
            all_errors += row.info->detail;
        }
        all_errors += "\n";
    }

    struct OpsButtonSpec {
        const char *label;
        std::function<void()> on_click;
    };

    const OpsButtonSpec buttons[] = {
        {"Reset Perception State", [&]() {
             ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::ResetPerceptionState}, runtime_ops_status);
         }},
        {"Copy All Errors", [&]() {
             ImGui::SetClipboardText(all_errors.c_str());
         }},
        {"Reset Error Counters", [&]() {
             ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::ResetErrorCounters}, runtime_ops_status);
         }},
        {"Export Runtime Snapshot (json)", [&]() {
             ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::ExportRuntimeSnapshot}, runtime_ops_status);
         }},
        {"Single-step Sampling", [&]() {
             ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::TriggerSingleStepSampling}, runtime_ops_status);
         }},
    };

    const float avail_width = ImGui::GetContentRegionAvail().x;
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float line_width = 0.0f;

    for (const auto &btn : buttons) {
        const ImVec2 text_size = ImGui::CalcTextSize(btn.label);
        const float button_width = text_size.x + padding.x * 2.0f;
        const float next_width = (line_width == 0.0f) ? button_width : (line_width + spacing + button_width);
        if (next_width > avail_width && line_width > 0.0f) {
            line_width = 0.0f;
        }
        if (line_width > 0.0f) {
            ImGui::SameLine();
        }
        if (ImGui::Button(btn.label)) {
            btn.on_click();
        }
        line_width = (line_width == 0.0f) ? button_width : (line_width + spacing + button_width);
    }

    model.runtime_ops_status = runtime_ops_status;
    ImGui::TextWrapped("%s", model.runtime_ops_status.empty() ? "" : model.runtime_ops_status.c_str());
}

void RenderRuntimePluginManagement(AppRuntime &runtime) {
    std::string &runtime_ops_status = RuntimeOpsStatusStorage();
    runtime_ops_status = runtime.runtime_ops_status;
    const UiCommandBridge bridge = BuildUiCommandBridge(runtime);
    ImGui::BeginChild("ops_plugin_child", ImVec2(-1.0f, 220.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Plugin 管理");
    ImGui::Text("Plugin Ready: %s", runtime.plugin_ready ? "true" : "false");
    if (!runtime.plugin_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Plugin Error: %s", runtime.plugin_last_error.c_str());
    }

    if (ImGui::Button("Refresh Plugin List")) {
        runtime.plugin_config_refresh_requested = true;
        RefreshPluginConfigs(runtime);
    }
    if (!runtime.plugin_config_scan_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Scan Error: %s", runtime.plugin_config_scan_error.c_str());
    }

    ImGui::BeginGroup();
    if (ImGui::BeginListBox("##plugin_config_list", ImVec2(-1.0f, 88.0f))) {
        for (int i = 0; i < static_cast<int>(runtime.plugin_config_entries.size()); ++i) {
            const auto &entry = runtime.plugin_config_entries[static_cast<std::size_t>(i)];
            const bool selected = (i == runtime.plugin_selected_entry_index);
            ImGui::PushID(i);
            if (ImGui::Selectable(entry.name.c_str(), selected)) {
                runtime.plugin_selected_entry_index = i;
                SDL_strlcpy(runtime.plugin_name_input, entry.name.c_str(), sizeof(runtime.plugin_name_input));
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    ImGui::EndGroup();

    if (runtime.plugin_selected_entry_index >= 0 &&
        runtime.plugin_selected_entry_index < static_cast<int>(runtime.plugin_config_entries.size())) {
        const auto &entry = runtime.plugin_config_entries[static_cast<std::size_t>(runtime.plugin_selected_entry_index)];
        ImGui::SeparatorText("Selected Behavior Plugin");
        ImGui::Text("Name: %s", entry.name.c_str());
        if (!entry.model_id.empty()) {
            ImGui::Text("Model Id: %s", entry.model_id.c_str());
        }
        if (!entry.model_version.empty()) {
            ImGui::Text("Version: %s", entry.model_version.c_str());
        }
        std::string config_path = entry.config_path;
        RenderLongTextBlock("Config Path", "behavior_plugin_config_path", &config_path, 3, 70.0f);
    } else {
        RenderUnifiedEmptyState("behavior_plugin_empty_state",
                                "未选择行为插件",
                                "从上方列表选择一个行为插件以查看配置路径与版本信息。",
                                ImVec4(0.72f, 0.82f, 1.0f, 1.0f));
    }

    ImGui::InputTextWithHint("Plugin Name", "输入插件名称", runtime.plugin_name_input, sizeof(runtime.plugin_name_input));
    if (ImGui::Button("Switch Plugin")) {
        std::string err;
        const bool ok = SwitchPluginByName(runtime, runtime.plugin_name_input, &err);
        if (ok) {
            runtime.plugin_switch_status = "plugin switch queued";
            runtime.plugin_switch_error.clear();
        } else {
            runtime.plugin_switch_status.clear();
            runtime.plugin_switch_error = err.empty() ? "plugin switch failed" : err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Selected")) {
        if (runtime.plugin_selected_entry_index >= 0 &&
            runtime.plugin_selected_entry_index < static_cast<int>(runtime.plugin_config_entries.size())) {
            const auto &entry = runtime.plugin_config_entries[static_cast<std::size_t>(runtime.plugin_selected_entry_index)];
            SDL_strlcpy(runtime.plugin_name_input, entry.name.c_str(), sizeof(runtime.plugin_name_input));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Selected")) {
        if (runtime.plugin_selected_entry_index >= 0 &&
            runtime.plugin_selected_entry_index < static_cast<int>(runtime.plugin_config_entries.size())) {
            const auto &entry = runtime.plugin_config_entries[static_cast<std::size_t>(runtime.plugin_selected_entry_index)];
            std::string err;
            const bool ok = DeletePluginConfig(runtime, entry.config_path, &err);
            if (ok) {
                runtime.plugin_delete_status = "plugin deleted";
                runtime.plugin_delete_error.clear();
                runtime.plugin_switch_status.clear();
                runtime.plugin_switch_error.clear();
            } else {
                runtime.plugin_delete_status.clear();
                runtime.plugin_delete_error = err.empty() ? "plugin delete failed" : err;
            }
        } else {
            runtime.plugin_delete_status.clear();
            runtime.plugin_delete_error = "no plugin selected";
        }
    }

    if (!runtime.plugin_switch_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.plugin_switch_status.c_str());
    }
    if (!runtime.plugin_switch_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.plugin_switch_error.c_str());
    }
    if (!runtime.plugin_delete_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.plugin_delete_status.c_str());
    }
    if (!runtime.plugin_delete_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.plugin_delete_error.c_str());
    }
    ImGui::EndChild();


    ImGui::BeginChild("ops_asr_child", ImVec2(-1.0f, 220.0f), ImGuiChildFlags_Borders);
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

    ImGui::BeginChild("ops_ocr_child", ImVec2(-1.0f, 220.0f), ImGuiChildFlags_Borders);
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

    ImGui::BeginChild("ops_program_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SameLine();
    ImGui::TextDisabled("(Esc)");
    ImGui::EndChild();
}


}  // namespace desktoper2D
