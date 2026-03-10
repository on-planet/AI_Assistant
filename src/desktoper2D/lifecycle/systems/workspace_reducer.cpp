#include "desktoper2D/lifecycle/systems/workspace_reducer.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void ReduceSwitchWorkspaceMode(AppRuntime &runtime, const UiCommand &cmd) {
    runtime.workspace_mode = static_cast<WorkspaceMode>(cmd.int_value);

    if (runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset) {
        runtime.workspace_preset_apply_requested = true;
        runtime.workspace_dock_rebuild_requested = true;
        runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
    } else {
        if (!HasAnyWorkspaceChildWindowVisible(runtime)) {
            ApplyWorkspaceWindowVisibility(runtime, BuildWorkspaceDefaultVisibility(runtime.workspace_mode));
        }

        // 手动布局下切换 Workspace 时，强制重建当前工作区的 Dock，避免载入旧 ini 后出现面板内容空白。
        // 同时抑制自动回写，避免把这次临时重建的结果覆盖用户之前保存的手动布局。
        runtime.workspace_manual_layout_pending_load = false;
        runtime.workspace_manual_layout_save_suppressed = true;
        runtime.workspace_preset_apply_requested = false;
        runtime.workspace_dock_rebuild_requested = true;
        runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
    }
}

void ReduceApplyPresetLayout(AppRuntime &runtime, const UiCommand &) {
    runtime.workspace_layout_mode = WorkspaceLayoutMode::Preset;
    runtime.workspace_preset_apply_requested = true;
    runtime.workspace_dock_rebuild_requested = true;
    runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
}

void ReduceResetManualLayout(AppRuntime &runtime, const UiCommand &) {
    runtime.workspace_manual_docking_ini.clear();
    runtime.workspace_manual_layout_pending_load = false;
    runtime.workspace_manual_layout_save_suppressed = false;
    runtime.workspace_manual_layout_reset_requested = true;
    runtime.workspace_layout_mode = WorkspaceLayoutMode::Preset;
    runtime.workspace_preset_apply_requested = true;
    runtime.workspace_dock_rebuild_requested = true;
    runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
}

void ReduceToggleManualLayout(AppRuntime &runtime, const UiCommand &cmd) {
    runtime.workspace_layout_mode = cmd.bool_value ? WorkspaceLayoutMode::Manual : WorkspaceLayoutMode::Preset;
    if (runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual) {
        const bool has_saved_manual_layout = !runtime.workspace_manual_docking_ini.empty();
        runtime.workspace_manual_layout_pending_load = has_saved_manual_layout;
        runtime.workspace_manual_layout_save_suppressed = false;
        runtime.workspace_preset_apply_requested = false;
        runtime.workspace_dock_rebuild_requested = !has_saved_manual_layout;
    } else {
        runtime.workspace_manual_layout_save_suppressed = false;
        runtime.workspace_preset_apply_requested = true;
        runtime.workspace_dock_rebuild_requested = true;
    }
}

void ReduceForceDockRebuild(AppRuntime &runtime, const UiCommand &) {
    runtime.workspace_dock_rebuild_requested = true;
    runtime.workspace_preset_apply_requested = runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset;
    runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
}

}  // namespace desktoper2D
