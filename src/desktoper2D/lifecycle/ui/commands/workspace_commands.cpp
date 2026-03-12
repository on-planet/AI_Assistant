#include "desktoper2D/lifecycle/ui/commands/workspace_commands.h"

#include <array>

namespace desktoper2D {

void ApplyWorkspaceAction(const UiCommandBridge &bridge, const WorkspaceAction &action) {
    struct WorkspaceCommandMapping {
        WorkspaceActionType action_type;
        UiCommandType command_type;
        bool use_workspace_mode_int;
    };

    static const std::array<WorkspaceCommandMapping, 15> kWorkspaceCommandMappings = {{
        {WorkspaceActionType::SwitchWorkspaceMode, UiCommandType::SwitchWorkspaceMode, true},
        {WorkspaceActionType::ApplyPresetLayout, UiCommandType::ApplyPresetLayout, false},
        {WorkspaceActionType::ResetManualLayout, UiCommandType::ResetManualLayout, false},
        {WorkspaceActionType::ToggleManualLayout, UiCommandType::ToggleManualLayout, false},
        {WorkspaceActionType::ToggleOverviewWindow, UiCommandType::ToggleOverviewWindow, false},
        {WorkspaceActionType::ToggleEditorWindow, UiCommandType::ToggleEditorWindow, false},
        {WorkspaceActionType::ToggleTimelineWindow, UiCommandType::ToggleTimelineWindow, false},
        {WorkspaceActionType::TogglePerceptionWindow, UiCommandType::TogglePerceptionWindow, false},
        {WorkspaceActionType::ToggleMappingWindow, UiCommandType::ToggleMappingWindow, false},
        {WorkspaceActionType::ToggleAsrChatWindow, UiCommandType::ToggleAsrChatWindow, false},
        {WorkspaceActionType::ToggleErrorWindow, UiCommandType::ToggleErrorWindow, false},
        {WorkspaceActionType::ToggleInspectorWindow, UiCommandType::ToggleInspectorWindow, false},
        {WorkspaceActionType::ToggleReminderWindow, UiCommandType::ToggleReminderWindow, false},
        {WorkspaceActionType::TogglePluginQuickControlWindow, UiCommandType::TogglePluginQuickControlWindow, false},
        {WorkspaceActionType::ForceDockRebuild, UiCommandType::ForceDockRebuild, false},
    }};

    for (const auto &mapping : kWorkspaceCommandMappings) {
        if (mapping.action_type != action.type) {
            continue;
        }
        UiCommand cmd{};
        cmd.type = mapping.command_type;
        cmd.bool_value = action.bool_value;
        if (mapping.use_workspace_mode_int) {
            cmd.int_value = static_cast<int>(action.workspace_mode);
        }
        bridge.Enqueue(cmd);
        return;
    }
}

void ApplyWorkspaceAction(AppRuntime &runtime, const WorkspaceAction &action) {
    ApplyWorkspaceAction(BuildUiCommandBridge(runtime), action);
}

}  // namespace desktoper2D
