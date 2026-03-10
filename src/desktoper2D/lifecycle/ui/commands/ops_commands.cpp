#include "desktoper2D/lifecycle/ui/commands/ops_commands.h"

#include <array>

namespace desktoper2D {

void ApplyOpsAction(const UiCommandBridge &bridge, const OpsAction &action, std::string &runtime_ops_status) {
    struct OpsCommandMapping {
        OpsActionType action_type;
        UiCommandType command_type;
    };

    static const std::array<OpsCommandMapping, 5> kOpsCommandMappings = {{
        {OpsActionType::ResetPerceptionState, UiCommandType::ResetPerceptionState},
        {OpsActionType::ResetErrorCounters, UiCommandType::ResetErrorCounters},
        {OpsActionType::ExportRuntimeSnapshot, UiCommandType::ExportRuntimeSnapshot},
        {OpsActionType::TriggerSingleStepSampling, UiCommandType::TriggerSingleStepSampling},
        {OpsActionType::CloseProgram, UiCommandType::CloseProgram},
    }};

    for (const auto &mapping : kOpsCommandMappings) {
        if (mapping.action_type != action.type) {
            continue;
        }
        UiCommand cmd{};
        cmd.type = mapping.command_type;
        bridge.Enqueue(cmd);
        runtime_ops_status = "Command queued";
        bridge.SetOpsStatus(runtime_ops_status);
        return;
    }
}

void ApplyOpsAction(AppRuntime &runtime, const OpsAction &action, std::string &runtime_ops_status) {
    ApplyOpsAction(BuildUiCommandBridge(runtime), action, runtime_ops_status);
}

}  // namespace desktoper2D
