#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

namespace desktoper2D {

enum class WorkspaceActionType {
    SwitchWorkspaceMode,
    ApplyPresetLayout,
    ResetManualLayout,
    ToggleManualLayout,
    ToggleOverviewWindow,
    ToggleEditorWindow,
    ToggleTimelineWindow,
    TogglePerceptionWindow,
    ToggleMappingWindow,
    ToggleOcrWindow,
    ToggleAsrChatWindow,
    TogglePluginWorkerWindow,
    ToggleChatWindow,
    ToggleErrorWindow,
    ToggleInspectorWindow,
    ToggleReminderWindow,
    TogglePluginQuickControlWindow,
    ForceDockRebuild,
};

struct WorkspaceAction {
    WorkspaceActionType type = WorkspaceActionType::SwitchWorkspaceMode;
    WorkspaceMode workspace_mode = WorkspaceMode::Animation;
    bool bool_value = false;
};

void ApplyWorkspaceAction(AppRuntime &runtime, const WorkspaceAction &action);
void ApplyWorkspaceAction(const UiCommandBridge &bridge, const WorkspaceAction &action);

}  // namespace desktoper2D
