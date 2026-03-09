#pragma once

#include "k2d/lifecycle/ui/app_debug_ui_panel_state.h"

namespace k2d {

void EnqueueUiCommand(AppRuntime &runtime, const UiCommand &cmd);

struct UiCommandBridge {
    using EnqueueFn = void (*)(void *ctx, const UiCommand &cmd);
    using SetOpsStatusFn = void (*)(void *ctx, const std::string &status);

    void *ctx = nullptr;
    EnqueueFn enqueue = nullptr;
    SetOpsStatusFn set_ops_status = nullptr;

    void Enqueue(const UiCommand &cmd) const {
        if (enqueue) {
            enqueue(ctx, cmd);
        }
    }

    void SetOpsStatus(const std::string &status) const {
        if (set_ops_status) {
            set_ops_status(ctx, status);
        }
    }
};

UiCommandBridge BuildUiCommandBridge(AppRuntime &runtime);

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
    ToggleAsrChatWindow,
    ToggleErrorWindow,
    ToggleOpsWindow,
    ToggleInspectorWindow,
    ToggleReminderWindow,
    ForceDockRebuild,
};

struct WorkspaceAction {
    WorkspaceActionType type = WorkspaceActionType::SwitchWorkspaceMode;
    WorkspaceMode workspace_mode = WorkspaceMode::Animation;
    bool bool_value = false;
};

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

void ApplyTimelinePanelActionImpl(AppRuntime &runtime, const TimelinePanelAction &action);
void ApplyEditorPanelActionImpl(AppRuntime &runtime, const EditorPanelAction &action);
void ApplyWorkspaceAction(AppRuntime &runtime, const WorkspaceAction &action);
void ApplyWorkspaceAction(const UiCommandBridge &bridge, const WorkspaceAction &action);
void ApplyOpsAction(AppRuntime &runtime, const OpsAction &action, std::string &runtime_ops_status);
void ApplyOpsAction(const UiCommandBridge &bridge, const OpsAction &action, std::string &runtime_ops_status);
void ConsumeUiCommandQueue(AppRuntime &runtime);

}  // namespace k2d
