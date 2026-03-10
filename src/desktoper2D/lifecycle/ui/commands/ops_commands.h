#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

namespace desktoper2D {

enum class OpsActionType {
    ResetPerceptionState,
    ResetErrorCounters,
    ExportRuntimeSnapshot,
    TriggerSingleStepSampling,
    CloseProgram,
};

struct OpsAction {
    OpsActionType type = OpsActionType::ResetPerceptionState;
};

void ApplyOpsAction(AppRuntime &runtime, const OpsAction &action, std::string &runtime_ops_status);
void ApplyOpsAction(const UiCommandBridge &bridge, const OpsAction &action, std::string &runtime_ops_status);

}  // namespace desktoper2D
