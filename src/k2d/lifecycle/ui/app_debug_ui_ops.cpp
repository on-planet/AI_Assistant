#include "app_debug_ui_actions.h"
#include "k2d/lifecycle/ui/app_debug_ui.h"

#include "k2d/lifecycle/ui/app_debug_ui_internal.h"

namespace k2d {

void RenderRuntimeOpsPanel(AppRuntime &runtime) {
    std::string &runtime_ops_status = RuntimeOpsStatusStorage();
    runtime_ops_status = runtime.runtime_ops_status;
    const UiCommandBridge bridge = BuildUiCommandBridge(runtime);
    OpsReadModel model = BuildOpsReadModel(runtime, runtime_ops_status);
    ImGui::BeginChild("ops_actions_child", ImVec2(-1.0f, 150.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Runtime Operations");
        if (ImGui::Button("Reset Perception State")) {
            ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::ResetPerceptionState}, runtime_ops_status);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Error Counters")) {
            ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::ResetErrorCounters}, runtime_ops_status);
        }

        if (ImGui::Button("Export Runtime Snapshot (json)")) {
            ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::ExportRuntimeSnapshot}, runtime_ops_status);
        }
        ImGui::SameLine();
        if (ImGui::Button("Single-step Sampling")) {
            ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::TriggerSingleStepSampling}, runtime_ops_status);
        }

        model.runtime_ops_status = runtime_ops_status;
        ImGui::TextWrapped("%s", model.runtime_ops_status.empty() ? "(no operation yet)" : model.runtime_ops_status.c_str());
    ImGui::EndChild();

    ImGui::BeginChild("ops_program_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Program");
    if (ImGui::Button("Close Program")) {
        ApplyOpsAction(bridge, OpsAction{.type = OpsActionType::CloseProgram}, runtime_ops_status);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Esc)");
    ImGui::EndChild();
}


}  // namespace k2d
