#include "app_debug_ui_actions.h"
#include "k2d/lifecycle/ui/app_debug_ui.h"

#include "k2d/lifecycle/ui/app_debug_ui_internal.h"

namespace k2d {

void RenderWorkspaceToolbar(AppRuntime &runtime) {
    const UiCommandBridge bridge = BuildUiCommandBridge(runtime);
    const WorkspaceReadModel model = BuildWorkspaceReadModel(runtime);
    const char *workspace_label = model.workspace_label;
    const char *layout_label = model.layout_label;

    if (ImGui::BeginMenu("Workspace")) {
        if (ImGui::MenuItem("Animation", nullptr, model.workspace_mode == WorkspaceMode::Animation)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::SwitchWorkspaceMode,
                                                         .workspace_mode = WorkspaceMode::Animation});
        }
        if (ImGui::MenuItem("Debug", nullptr, model.workspace_mode == WorkspaceMode::Debug)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::SwitchWorkspaceMode,
                                                         .workspace_mode = WorkspaceMode::Debug});
        }
        if (ImGui::MenuItem("Perception", nullptr, model.workspace_mode == WorkspaceMode::Perception)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::SwitchWorkspaceMode,
                                                         .workspace_mode = WorkspaceMode::Perception});
        }
        if (ImGui::MenuItem("Authoring", nullptr, model.workspace_mode == WorkspaceMode::Authoring)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::SwitchWorkspaceMode,
                                                         .workspace_mode = WorkspaceMode::Authoring});
        }
        ImGui::Separator();
        ImGui::TextDisabled("Current: %s", workspace_label);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Apply Preset Layout")) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ApplyPresetLayout});
        }
        if (ImGui::MenuItem("Reset Manual Layout")) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ResetManualLayout});
        }
        bool use_manual_layout = model.layout_mode == WorkspaceLayoutMode::Manual;
        if (ImGui::MenuItem("Manual Layout", nullptr, &use_manual_layout)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleManualLayout,
                                                         .bool_value = use_manual_layout});
        }
        ImGui::Separator();
        ImGui::TextDisabled("Layout: %s", layout_label);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows")) {
        bool show_overview = runtime.show_overview_window;
        if (ImGui::MenuItem("Overview", nullptr, &show_overview)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleOverviewWindow, .bool_value = show_overview});
        }
        bool show_editor = runtime.show_editor_window;
        if (ImGui::MenuItem("Editor", nullptr, &show_editor)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleEditorWindow, .bool_value = show_editor});
        }
        bool show_timeline = runtime.show_timeline_window;
        if (ImGui::MenuItem("Timeline", nullptr, &show_timeline)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleTimelineWindow, .bool_value = show_timeline});
        }
        bool show_perception = runtime.show_perception_window;
        if (ImGui::MenuItem("Perception", nullptr, &show_perception)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::TogglePerceptionWindow, .bool_value = show_perception});
        }
        bool show_mapping = runtime.show_mapping_window;
        if (ImGui::MenuItem("Mapping", nullptr, &show_mapping)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleMappingWindow, .bool_value = show_mapping});
        }
        bool show_asr_chat = runtime.show_asr_chat_window;
        if (ImGui::MenuItem("ASR/Chat", nullptr, &show_asr_chat)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleAsrChatWindow, .bool_value = show_asr_chat});
        }
        bool show_error = runtime.show_error_window;
        if (ImGui::MenuItem("Errors", nullptr, &show_error)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleErrorWindow, .bool_value = show_error});
        }
        bool show_ops = runtime.show_ops_window;
        if (ImGui::MenuItem("Ops", nullptr, &show_ops)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleOpsWindow, .bool_value = show_ops});
        }
        bool show_inspector = runtime.show_inspector_window;
        if (ImGui::MenuItem("Inspector", nullptr, &show_inspector)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleInspectorWindow, .bool_value = show_inspector});
        }
        bool show_reminder = runtime.show_reminder_window;
        if (ImGui::MenuItem("Reminder", nullptr, &show_reminder)) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ToggleReminderWindow, .bool_value = show_reminder});
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
        if (ImGui::MenuItem("Force Dock Rebuild")) {
            ApplyWorkspaceAction(bridge, WorkspaceAction{.type = WorkspaceActionType::ForceDockRebuild});
        }
        ImGui::Separator();
        ImGui::TextDisabled("dock_rebuild=%s", model.dock_rebuild_requested ? "true" : "false");
        ImGui::TextDisabled("preset_apply=%s", model.preset_apply_requested ? "true" : "false");
        ImGui::EndMenu();
    }
}

}  // namespace k2d
