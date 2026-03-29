#include "desktoper2D/lifecycle/systems/workspace_reducer.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

namespace {

void ClearManualLayoutSaveRequest(WorkspacePanelState &panels) {
    panels.manual_layout_save_pending = false;
    panels.manual_layout_save_debounce_remaining_sec = 0.0f;
}

}  // namespace

void ReduceSwitchWorkspaceMode(AppRuntime &runtime, const UiCommand &cmd) {
    auto &workspace = runtime.workspace_ui;
    auto &panels = workspace.panels;
    workspace.mode = static_cast<WorkspaceMode>(cmd.int_value);

    if (panels.layout_mode == WorkspaceLayoutMode::Preset) {
        panels.preset_apply_requested = true;
        workspace.dock_rebuild_requested = true;
        panels.last_applied_mode = static_cast<WorkspaceMode>(-1);
    } else {
        if (!HasAnyWorkspaceChildWindowVisible(runtime)) {
            ApplyWorkspaceWindowVisibility(runtime, BuildWorkspaceDefaultVisibility(workspace.mode));
        }

        // 手动布局下切换 Workspace 时，强制重建当前工作区的 Dock，避免载入旧 ini 后出现面板内容空白。
        // 同时抑制自动回写，避免把这次临时重建的结果覆盖用户之前保存的手动布局。
        panels.manual_layout_pending_load = false;
        panels.manual_layout_save_suppressed = true;
        panels.preset_apply_requested = false;
        workspace.dock_rebuild_requested = true;
        panels.last_applied_mode = static_cast<WorkspaceMode>(-1);
        ClearManualLayoutSaveRequest(panels);
    }
}

void ReduceApplyPresetLayout(AppRuntime &runtime, const UiCommand &) {
    runtime.workspace_ui.panels.layout_mode = WorkspaceLayoutMode::Preset;
    runtime.workspace_ui.panels.preset_apply_requested = true;
    runtime.workspace_ui.dock_rebuild_requested = true;
    runtime.workspace_ui.panels.last_applied_mode = static_cast<WorkspaceMode>(-1);
    ClearManualLayoutSaveRequest(runtime.workspace_ui.panels);
}

void ReduceResetManualLayout(AppRuntime &runtime, const UiCommand &) {
    auto &workspace = runtime.workspace_ui;
    auto &panels = workspace.panels;
    panels.manual_docking_ini.clear();
    panels.manual_layout_pending_load = false;
    panels.manual_layout_save_suppressed = false;
    panels.manual_layout_reset_requested = true;
    panels.layout_mode = WorkspaceLayoutMode::Preset;
    panels.preset_apply_requested = true;
    workspace.dock_rebuild_requested = true;
    panels.last_applied_mode = static_cast<WorkspaceMode>(-1);
    ClearManualLayoutSaveRequest(panels);
}

void ReduceToggleManualLayout(AppRuntime &runtime, const UiCommand &cmd) {
    auto &workspace = runtime.workspace_ui;
    auto &panels = workspace.panels;
    panels.layout_mode = cmd.bool_value ? WorkspaceLayoutMode::Manual : WorkspaceLayoutMode::Preset;
    if (panels.layout_mode == WorkspaceLayoutMode::Manual) {
        const bool has_saved_manual_layout = !panels.manual_docking_ini.empty();
        panels.manual_layout_pending_load = has_saved_manual_layout;
        panels.manual_layout_save_suppressed = false;
        panels.preset_apply_requested = false;
        workspace.dock_rebuild_requested = !has_saved_manual_layout;
    } else {
        panels.manual_layout_save_suppressed = false;
        panels.preset_apply_requested = true;
        workspace.dock_rebuild_requested = true;
    }
    ClearManualLayoutSaveRequest(panels);
}

void ReduceForceDockRebuild(AppRuntime &runtime, const UiCommand &) {
    runtime.workspace_ui.dock_rebuild_requested = true;
    runtime.workspace_ui.panels.preset_apply_requested =
        runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Preset;
    runtime.workspace_ui.panels.last_applied_mode = static_cast<WorkspaceMode>(-1);
    ClearManualLayoutSaveRequest(runtime.workspace_ui.panels);
}

}  // namespace desktoper2D
