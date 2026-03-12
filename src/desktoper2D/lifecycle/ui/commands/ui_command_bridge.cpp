#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

#include <array>
#include <optional>
#include <vector>

#include "desktoper2D/lifecycle/systems/runtime_command_executor.h"
#include "desktoper2D/lifecycle/systems/workspace_reducer.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_runtime_actions.h"

namespace desktoper2D {

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

void ReduceToggleOverviewWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_overview_window = cmd.bool_value; }
void ReduceToggleEditorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_editor_window = cmd.bool_value; }
void ReduceToggleTimelineWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_timeline_window = cmd.bool_value; }
void ReduceTogglePerceptionWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_perception_window = cmd.bool_value; }
void ReduceToggleMappingWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_mapping_window = cmd.bool_value; }
void ReduceToggleAsrChatWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_asr_chat_window = cmd.bool_value; }
void ReduceToggleErrorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_error_window = cmd.bool_value; }
void ReduceToggleInspectorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_inspector_window = cmd.bool_value; }
void ReduceToggleReminderWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_reminder_window = cmd.bool_value; }
void ReduceTogglePluginQuickControlWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.show_plugin_quick_control_window = cmd.bool_value; }

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

const std::array<UiCommandReducerEntry, 21> kUiCommandReducerTable = {{
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
    {UiCommandType::ToggleInspectorWindow, &ReduceToggleInspectorWindow},
    {UiCommandType::ToggleReminderWindow, &ReduceToggleReminderWindow},
    {UiCommandType::TogglePluginQuickControlWindow, &ReduceTogglePluginQuickControlWindow},
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

UiCommandBridge BuildUiCommandBridge(AppRuntime &runtime) {
    return UiCommandBridge{
        .ctx = &runtime,
        .enqueue = [](void *ctx, const UiCommand &cmd) {
            auto &rt = *static_cast<AppRuntime *>(ctx);
            EnqueueUiCommand(rt, cmd);
        },
        .set_ops_status = [](void *ctx, const std::string &status) {
            auto &rt = *static_cast<AppRuntime *>(ctx);
            rt.runtime_ops_status = status;
        },
    };
}

void EnqueueUiCommand(AppRuntime &runtime, const UiCommand &cmd) {
    auto &bus = runtime.command_bus;
    if (bus.ui_command_queue_capacity == 0) {
        return;
    }
    if (bus.ui_command_queue.size() >= bus.ui_command_queue_capacity) {
        if (!bus.ui_command_queue.empty()) {
            bus.ui_command_queue.erase(bus.ui_command_queue.begin());
        }
        PushRuntimeEvent(runtime,
                         RuntimeEvent{.type = RuntimeEventType::UiCommandDroppedQueueFull,
                                      .command_type = cmd.type,
                                      .int_value = cmd.int_value,
                                      .bool_value = cmd.bool_value,
                                      .message = "ui command dropped: queue full (oldest evicted)"});
    }
    bus.ui_command_queue.resize(bus.ui_command_queue.size() + 1);
    bus.ui_command_queue.back() = cmd;
    PushRuntimeEvent(runtime,
                     RuntimeEvent{.type = RuntimeEventType::UiCommandQueued,
                                  .command_type = cmd.type,
                                  .int_value = cmd.int_value,
                                  .bool_value = cmd.bool_value,
                                  .message = "ui command queued"});
}

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

}  // namespace desktoper2D
