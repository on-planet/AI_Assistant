#include "k2d/lifecycle/systems/runtime_command_executor.h"

#include "k2d/lifecycle/ui/app_debug_ui_internal.h"

#include <array>
#include <optional>

namespace k2d {

void PushRuntimeEvent(AppRuntime &runtime, const RuntimeEvent &event) {
    auto &bus = runtime.command_bus;
    if (bus.runtime_event_queue_capacity == 0) {
        return;
    }
    if (bus.runtime_event_queue.size() >= bus.runtime_event_queue_capacity) {
        bus.runtime_event_queue.pop_front();
    }
    bus.runtime_event_queue.push_back(event);
}

namespace {

using UiCommandReducer = void (*)(AppRuntime &, const UiCommand &);

struct UiCommandReducerEntry {
    UiCommandType type;
    UiCommandReducer reducer;
};

void EmitOpsStatusUpdated(AppRuntime &runtime, const UiCommand &cmd) {
    PushRuntimeEvent(runtime,
                     RuntimeEvent{.type = RuntimeEventType::OpsStatusUpdated,
                                  .command_type = cmd.type,
                                  .message = runtime.runtime_ops_status});
}

void ReduceSwitchWorkspaceMode(AppRuntime &runtime, const UiCommand &cmd) {
    runtime.workspace_mode = static_cast<WorkspaceMode>(cmd.int_value);

    if (runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset) {
        runtime.workspace_preset_apply_requested = true;
        runtime.workspace_dock_rebuild_requested = true;
        runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
    } else {
        bool any_window_visible = false;
        any_window_visible = any_window_visible || runtime.show_overview_window;
        any_window_visible = any_window_visible || runtime.show_editor_window;
        any_window_visible = any_window_visible || runtime.show_timeline_window;
        any_window_visible = any_window_visible || runtime.show_perception_window;
        any_window_visible = any_window_visible || runtime.show_mapping_window;
        any_window_visible = any_window_visible || runtime.show_asr_chat_window;
        any_window_visible = any_window_visible || runtime.show_error_window;
        any_window_visible = any_window_visible || runtime.show_ops_window;
        any_window_visible = any_window_visible || runtime.show_inspector_window;
        any_window_visible = any_window_visible || runtime.show_reminder_window;

        if (!any_window_visible) {
            runtime.show_overview_window = true;
            runtime.show_error_window = true;
            runtime.show_ops_window = true;
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

void ReduceToggleOverviewWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_overview_window = cmd.bool_value; }
void ReduceToggleEditorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_editor_window = cmd.bool_value; }
void ReduceToggleTimelineWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_timeline_window = cmd.bool_value; }
void ReduceTogglePerceptionWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_perception_window = cmd.bool_value; }
void ReduceToggleMappingWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_mapping_window = cmd.bool_value; }
void ReduceToggleAsrChatWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_asr_chat_window = cmd.bool_value; }
void ReduceToggleErrorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_error_window = cmd.bool_value; }
void ReduceToggleOpsWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_ops_window = cmd.bool_value; }
void ReduceToggleInspectorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_inspector_window = cmd.bool_value; }
void ReduceToggleReminderWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_reminder_window = cmd.bool_value; }

void ReduceForceDockRebuild(AppRuntime &runtime, const UiCommand &) {
    runtime.workspace_dock_rebuild_requested = true;
    runtime.workspace_preset_apply_requested = runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset;
    runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);
}

void ReduceResetPerceptionState(AppRuntime &runtime, const UiCommand &cmd) {
    ResetPerceptionRuntimeState(runtime.perception_state);
    runtime.runtime_ops_status = "Perception state reset";
    EmitOpsStatusUpdated(runtime, cmd);
}

void ReduceResetErrorCounters(AppRuntime &runtime, const UiCommand &cmd) {
    ResetAllRuntimeErrorCounters(runtime);
    runtime.runtime_ops_status = "Runtime error counters reset";
    EmitOpsStatusUpdated(runtime, cmd);
}

void ReduceExportRuntimeSnapshot(AppRuntime &runtime, const UiCommand &cmd) {
    std::string err;
    if (ExportRuntimeSnapshotJson(runtime, "assets/runtime_snapshot.json", &err)) {
        runtime.runtime_ops_status = "Snapshot exported: assets/runtime_snapshot.json";
    } else {
        runtime.runtime_ops_status = "Snapshot export failed: " + err;
    }
    EmitOpsStatusUpdated(runtime, cmd);
}

void ReduceTriggerSingleStepSampling(AppRuntime &runtime, const UiCommand &cmd) {
    TriggerSingleStepSampling(runtime);
    runtime.runtime_ops_status = "Single-step sampling triggered";
    EmitOpsStatusUpdated(runtime, cmd);
}

void ReduceCloseProgram(AppRuntime &runtime, const UiCommand &cmd) {
    runtime.running = false;
    PushRuntimeEvent(runtime,
                     RuntimeEvent{.type = RuntimeEventType::ProgramCloseRequested,
                                  .command_type = cmd.type,
                                  .message = "Program close requested"});
}

const std::array<UiCommandReducerEntry, 20> kUiCommandReducerTable = {{
    {UiCommandType::SwitchWorkspaceMode, &ReduceSwitchWorkspaceMode},
    {UiCommandType::ApplyPresetLayout, &ReduceApplyPresetLayout},
    {UiCommandType::ResetManualLayout, &ReduceResetManualLayout},
    {UiCommandType::ToggleManualLayout, &ReduceToggleManualLayout},
    {UiCommandType::ToggleOverviewWindow, &ReduceToggleOverviewWindow},
    {UiCommandType::ToggleEditorWindow, &ReduceToggleEditorWindow},
    {UiCommandType::ToggleTimelineWindow, &ReduceToggleTimelineWindow},
    {UiCommandType::TogglePerceptionWindow, &ReduceTogglePerceptionWindow},
    {UiCommandType::ToggleMappingWindow, &ReduceToggleMappingWindow},
    {UiCommandType::ToggleAsrChatWindow, &ReduceToggleAsrChatWindow},
    {UiCommandType::ToggleErrorWindow, &ReduceToggleErrorWindow},
    {UiCommandType::ToggleOpsWindow, &ReduceToggleOpsWindow},
    {UiCommandType::ToggleInspectorWindow, &ReduceToggleInspectorWindow},
    {UiCommandType::ToggleReminderWindow, &ReduceToggleReminderWindow},
    {UiCommandType::ForceDockRebuild, &ReduceForceDockRebuild},
    {UiCommandType::ResetPerceptionState, &ReduceResetPerceptionState},
    {UiCommandType::ResetErrorCounters, &ReduceResetErrorCounters},
    {UiCommandType::ExportRuntimeSnapshot, &ReduceExportRuntimeSnapshot},
    {UiCommandType::TriggerSingleStepSampling, &ReduceTriggerSingleStepSampling},
    {UiCommandType::CloseProgram, &ReduceCloseProgram},
}};

std::optional<UiCommandReducer> FindUiCommandReducer(UiCommandType type) {
    for (const auto &entry : kUiCommandReducerTable) {
        if (entry.type == type) {
            return entry.reducer;
        }
    }
    return std::nullopt;
}

}  // namespace

void ExecuteUiCommand(AppRuntime &runtime, const UiCommand &cmd) {
    const std::optional<UiCommandReducer> reducer = FindUiCommandReducer(cmd.type);
    if (!reducer.has_value()) {
        return;
    }
    (*reducer)(runtime, cmd);
}

void ConsumeUiCommandQueue(AppRuntime &runtime) {
    auto &bus = runtime.command_bus;
    if (bus.ui_command_queue.empty()) {
        return;
    }

    const std::vector<UiCommand> queued_commands = bus.ui_command_queue;
    bus.ui_command_queue.clear();

    for (const UiCommand &cmd : queued_commands) {
        ExecuteUiCommand(runtime, cmd);
        PushRuntimeEvent(runtime,
                         RuntimeEvent{.type = RuntimeEventType::UiCommandConsumed,
                                      .command_type = cmd.type,
                                      .int_value = cmd.int_value,
                                      .bool_value = cmd.bool_value,
                                      .message = "ui command consumed"});
    }
}

}  // namespace k2d
