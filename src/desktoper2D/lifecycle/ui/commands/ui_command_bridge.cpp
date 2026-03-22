#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

#include <array>
#include <optional>
#include <vector>

#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/systems/runtime_command_executor.h"
#include "desktoper2D/lifecycle/systems/workspace_reducer.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_runtime_actions.h"
#include "desktoper2D/lifecycle/ui/commands/plugin_commands.h"

namespace desktoper2D {

namespace {

using UiCommandReducer = void (*)(AppRuntime &, const UiCommand &);

struct UiCommandReducerEntry {
    UiCommandType type;
    UiCommandReducer reducer;
};

struct PluginFeedbackRefs {
    std::string *status = nullptr;
    std::string *error = nullptr;
};

PluginActionFeedbackSlot DecodePluginFeedbackSlot(int raw_value) {
    switch (static_cast<PluginActionFeedbackSlot>(raw_value)) {
        case PluginActionFeedbackSlot::Switch:
        case PluginActionFeedbackSlot::AsrSwitch:
        case PluginActionFeedbackSlot::OcrSwitch:
        case PluginActionFeedbackSlot::OverrideApply:
        case PluginActionFeedbackSlot::UnifiedSwitch:
        case PluginActionFeedbackSlot::UnifiedCreate:
            return static_cast<PluginActionFeedbackSlot>(raw_value);
        case PluginActionFeedbackSlot::None:
        default:
            return PluginActionFeedbackSlot::None;
    }
}

PluginFeedbackRefs LookupPluginFeedbackRefs(AppRuntime &runtime, PluginActionFeedbackSlot slot) {
    switch (slot) {
        case PluginActionFeedbackSlot::Switch:
            return PluginFeedbackRefs{&runtime.plugin.switch_status, &runtime.plugin.switch_error};
        case PluginActionFeedbackSlot::AsrSwitch:
            return PluginFeedbackRefs{&runtime.plugin.asr_switch_status, &runtime.plugin.asr_switch_error};
        case PluginActionFeedbackSlot::OcrSwitch:
            return PluginFeedbackRefs{&runtime.plugin.ocr_switch_status, &runtime.plugin.ocr_switch_error};
        case PluginActionFeedbackSlot::OverrideApply:
            return PluginFeedbackRefs{&runtime.plugin.override_apply_status, &runtime.plugin.override_apply_error};
        case PluginActionFeedbackSlot::UnifiedSwitch:
            return PluginFeedbackRefs{&runtime.plugin.unified_switch_status, &runtime.plugin.unified_switch_error};
        case PluginActionFeedbackSlot::UnifiedCreate:
            return PluginFeedbackRefs{&runtime.plugin.unified_create_status, &runtime.plugin.unified_create_error};
        case PluginActionFeedbackSlot::None:
        default:
            return PluginFeedbackRefs{};
    }
}

PerceptionReloadTarget DecodePerceptionReloadTarget(const UiCommand &cmd) {
    if (cmd.text_value_3 == "asr") {
        return PerceptionReloadTarget::Asr;
    }
    if (cmd.text_value_3 == "scene") {
        return PerceptionReloadTarget::Scene;
    }
    if (cmd.text_value_3 == "ocr") {
        return PerceptionReloadTarget::Ocr;
    }
    if (cmd.text_value_3 == "facemesh") {
        return PerceptionReloadTarget::Facemesh;
    }
    return PerceptionReloadTarget::All;
}

void SetPluginFeedback(AppRuntime &runtime,
                       PluginActionFeedbackSlot slot,
                       bool ok,
                       const std::string &success_status,
                       const std::string &error_text) {
    PluginFeedbackRefs refs = LookupPluginFeedbackRefs(runtime, slot);
    if (refs.status == nullptr || refs.error == nullptr) {
        return;
    }
    if (ok) {
        *refs.status = success_status;
        refs.error->clear();
        return;
    }
    refs.status->clear();
    *refs.error = error_text;
}

void EmitOpsStatusUpdated(AppRuntime &runtime, const UiCommand &cmd) {
    PushRuntimeEvent(runtime,
                     RuntimeEvent{.type = RuntimeEventType::OpsStatusUpdated,
                                  .command_type = cmd.type,
                                  .message = runtime.runtime_ops_status});
}

void ReduceToggleOverviewWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_overview_window = cmd.bool_value; }
void ReduceToggleEditorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_editor_window = cmd.bool_value; }
void ReduceToggleTimelineWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_timeline_window = cmd.bool_value; }
void ReduceTogglePerceptionWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_perception_window = cmd.bool_value; }
void ReduceToggleMappingWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_mapping_window = cmd.bool_value; }
void ReduceToggleOcrWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_ocr_window = cmd.bool_value; }
void ReduceToggleAsrChatWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_asr_chat_window = cmd.bool_value; }
void ReduceTogglePluginWorkerWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_plugin_worker_window = cmd.bool_value; }
void ReduceToggleChatWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_chat_window = cmd.bool_value; }
void ReduceToggleErrorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_error_window = cmd.bool_value; }
void ReduceToggleInspectorWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_inspector_window = cmd.bool_value; }
void ReduceToggleReminderWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_reminder_window = cmd.bool_value; }
void ReduceTogglePluginQuickControlWindow(AppRuntime &runtime, const UiCommand &cmd) { runtime.workspace_ui.panels.show_plugin_quick_control_window = cmd.bool_value; }

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

void ReduceRefreshPluginConfigs(AppRuntime &runtime, const UiCommand &) { RefreshPluginConfigs(runtime); }

void ReduceSwitchPluginByName(AppRuntime &runtime, const UiCommand &cmd) {
    if (runtime.plugin.config_entries.empty()) {
        RefreshPluginConfigs(runtime);
    }

    std::string err;
    const bool ok = SwitchPluginByName(runtime, cmd.text_value, &err);
    if (ok) {
        runtime.plugin.switch_status = "plugin switch queued";
        runtime.plugin.switch_error.clear();
    } else {
        runtime.plugin.switch_status.clear();
        runtime.plugin.switch_error = err.empty() ? "plugin switch failed" : err;
    }
}

void ReduceDeletePluginConfig(AppRuntime &runtime, const UiCommand &cmd) {
    if (cmd.text_value.empty()) {
        runtime.plugin.delete_status.clear();
        runtime.plugin.delete_error = "no plugin selected";
        return;
    }

    std::string err;
    const bool ok = DeletePluginConfig(runtime, cmd.text_value, &err);
    if (ok) {
        runtime.plugin.delete_status = "plugin deleted";
        runtime.plugin.delete_error.clear();
        runtime.plugin.switch_status.clear();
        runtime.plugin.switch_error.clear();
    } else {
        runtime.plugin.delete_status.clear();
        runtime.plugin.delete_error = err.empty() ? "plugin delete failed" : err;
    }
}

void ReduceRefreshAsrProviders(AppRuntime &runtime, const UiCommand &) { RefreshAsrProviders(runtime); }

void ReduceSwitchAsrProviderByName(AppRuntime &runtime, const UiCommand &cmd) {
    if (runtime.plugin.asr_provider_entries.empty()) {
        RefreshAsrProviders(runtime);
    }

    std::string err;
    const bool ok = SwitchAsrProviderByName(runtime, cmd.text_value, &err);
    if (ok) {
        runtime.plugin.asr_switch_status = "asr switch ok";
        runtime.plugin.asr_switch_error.clear();
        runtime.plugin.asr_current_provider_name = cmd.text_value;
    } else {
        runtime.plugin.asr_switch_status.clear();
        runtime.plugin.asr_switch_error = err.empty() ? "asr switch failed" : err;
    }
}

void ReduceRefreshOcrModels(AppRuntime &runtime, const UiCommand &) { RefreshOcrModels(runtime); }

void ReduceSwitchOcrModelByName(AppRuntime &runtime, const UiCommand &cmd) {
    if (runtime.plugin.ocr_model_entries.empty()) {
        RefreshOcrModels(runtime);
    }

    std::string err;
    const bool ok = SwitchOcrModelByName(runtime, cmd.text_value, &err);
    if (ok) {
        runtime.plugin.ocr_switch_status = "ocr switch ok";
        runtime.plugin.ocr_switch_error.clear();
    } else {
        runtime.plugin.ocr_switch_status.clear();
        runtime.plugin.ocr_switch_error = err.empty() ? "ocr switch failed" : err;
    }
}

void ReduceRefreshUnifiedPlugins(AppRuntime &runtime, const UiCommand &) { RefreshUnifiedPlugins(runtime); }

void ReduceApplyOverrideModels(AppRuntime &runtime, const UiCommand &cmd) {
    std::string err;
    const bool ok = ApplyOverrideModels(runtime, DecodePerceptionReloadTarget(cmd), &err);
    if (ok) {
        RefreshUnifiedPlugins(runtime);
    }

    const std::string failure_text = err.empty()
                                         ? (cmd.text_value_2.empty() ? std::string("override apply failed")
                                                                     : cmd.text_value_2)
                                         : err;
    SetPluginFeedback(runtime,
                      DecodePluginFeedbackSlot(cmd.int_value),
                      ok,
                      cmd.text_value.empty() ? std::string("override apply ok") : cmd.text_value,
                      failure_text);
}

void ReduceReplaceUnifiedPluginAssets(AppRuntime &runtime, const UiCommand &cmd) {
    if (runtime.plugin.unified_entries.empty()) {
        RefreshUnifiedPlugins(runtime);
    }

    PluginAssetOverride override{};
    override.onnx = cmd.text_value_2;
    override.labels = cmd.text_value_3;
    override.keys = cmd.text_value_4;
    override.extra_onnx = cmd.text_list_value;

    std::string err;
    const bool ok = ReplaceUnifiedPluginAssets(runtime, cmd.text_value, override, &err);
    if (ok) {
        RefreshUnifiedPlugins(runtime);
    }

    const PluginActionFeedbackSlot slot = DecodePluginFeedbackSlot(cmd.int_value);
    const std::string success_status = slot == PluginActionFeedbackSlot::UnifiedSwitch
                                           ? std::string("plugin switch queued")
                                           : std::string("plugin switch queued");
    const std::string failure_text = err.empty() ? std::string("plugin switch failed") : err;
    SetPluginFeedback(runtime, slot, ok, success_status, failure_text);
}

void ReduceCreateUserPlugin(AppRuntime &runtime, const UiCommand &cmd) {
    UserPluginCreateRequest request{};
    request.name = cmd.text_value;
    request.template_path = cmd.text_value_2;
    request.use_folder_layout = cmd.bool_value;

    std::string err;
    const bool ok = CreateUserPlugin(runtime, request, &err);
    if (ok) {
        runtime.plugin.config_refresh_requested = true;
        runtime.plugin.unified_refresh_requested = true;
        RefreshUnifiedPlugins(runtime);
    }

    const std::string failure_text = err.empty() ? std::string("plugin create failed") : err;
    SetPluginFeedback(runtime,
                      DecodePluginFeedbackSlot(cmd.int_value),
                      ok,
                      "plugin created",
                      failure_text);
}

void ReduceCloseProgram(AppRuntime &runtime, const UiCommand &cmd) {
    runtime.running = false;
    PushRuntimeEvent(runtime,
                     RuntimeEvent{.type = RuntimeEventType::ProgramCloseRequested,
                                  .command_type = cmd.type,
                                  .message = "Program close requested"});
}

const std::array kUiCommandReducerTable = {
    UiCommandReducerEntry{UiCommandType::SwitchWorkspaceMode, &ReduceSwitchWorkspaceMode},
    UiCommandReducerEntry{UiCommandType::ApplyPresetLayout, &ReduceApplyPresetLayout},
    UiCommandReducerEntry{UiCommandType::ResetManualLayout, &ReduceResetManualLayout},
    UiCommandReducerEntry{UiCommandType::ToggleManualLayout, &ReduceToggleManualLayout},
    UiCommandReducerEntry{UiCommandType::ToggleOverviewWindow, &ReduceToggleOverviewWindow},
    UiCommandReducerEntry{UiCommandType::ToggleEditorWindow, &ReduceToggleEditorWindow},
    UiCommandReducerEntry{UiCommandType::ToggleTimelineWindow, &ReduceToggleTimelineWindow},
    UiCommandReducerEntry{UiCommandType::TogglePerceptionWindow, &ReduceTogglePerceptionWindow},
    UiCommandReducerEntry{UiCommandType::ToggleMappingWindow, &ReduceToggleMappingWindow},
    UiCommandReducerEntry{UiCommandType::ToggleOcrWindow, &ReduceToggleOcrWindow},
    UiCommandReducerEntry{UiCommandType::ToggleAsrChatWindow, &ReduceToggleAsrChatWindow},
    UiCommandReducerEntry{UiCommandType::TogglePluginWorkerWindow, &ReduceTogglePluginWorkerWindow},
    UiCommandReducerEntry{UiCommandType::ToggleChatWindow, &ReduceToggleChatWindow},
    UiCommandReducerEntry{UiCommandType::ToggleErrorWindow, &ReduceToggleErrorWindow},
    UiCommandReducerEntry{UiCommandType::ToggleInspectorWindow, &ReduceToggleInspectorWindow},
    UiCommandReducerEntry{UiCommandType::ToggleReminderWindow, &ReduceToggleReminderWindow},
    UiCommandReducerEntry{UiCommandType::TogglePluginQuickControlWindow, &ReduceTogglePluginQuickControlWindow},
    UiCommandReducerEntry{UiCommandType::ForceDockRebuild, &ReduceForceDockRebuild},
    UiCommandReducerEntry{UiCommandType::ResetPerceptionState, &ReduceResetPerceptionState},
    UiCommandReducerEntry{UiCommandType::ResetErrorCounters, &ReduceResetErrorCounters},
    UiCommandReducerEntry{UiCommandType::ExportRuntimeSnapshot, &ReduceExportRuntimeSnapshot},
    UiCommandReducerEntry{UiCommandType::TriggerSingleStepSampling, &ReduceTriggerSingleStepSampling},
    UiCommandReducerEntry{UiCommandType::RefreshPluginConfigs, &ReduceRefreshPluginConfigs},
    UiCommandReducerEntry{UiCommandType::SwitchPluginByName, &ReduceSwitchPluginByName},
    UiCommandReducerEntry{UiCommandType::DeletePluginConfig, &ReduceDeletePluginConfig},
    UiCommandReducerEntry{UiCommandType::RefreshAsrProviders, &ReduceRefreshAsrProviders},
    UiCommandReducerEntry{UiCommandType::SwitchAsrProviderByName, &ReduceSwitchAsrProviderByName},
    UiCommandReducerEntry{UiCommandType::RefreshOcrModels, &ReduceRefreshOcrModels},
    UiCommandReducerEntry{UiCommandType::SwitchOcrModelByName, &ReduceSwitchOcrModelByName},
    UiCommandReducerEntry{UiCommandType::RefreshUnifiedPlugins, &ReduceRefreshUnifiedPlugins},
    UiCommandReducerEntry{UiCommandType::ApplyOverrideModels, &ReduceApplyOverrideModels},
    UiCommandReducerEntry{UiCommandType::ReplaceUnifiedPluginAssets, &ReduceReplaceUnifiedPluginAssets},
    UiCommandReducerEntry{UiCommandType::CreateUserPlugin, &ReduceCreateUserPlugin},
    UiCommandReducerEntry{UiCommandType::CloseProgram, &ReduceCloseProgram},
};

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
            bus.ui_command_queue.pop_front();
        }
        PushRuntimeEvent(runtime,
                         RuntimeEvent{.type = RuntimeEventType::UiCommandDroppedQueueFull,
                                      .command_type = cmd.type,
                                      .int_value = cmd.int_value,
                                      .bool_value = cmd.bool_value,
                                      .message = "ui command dropped: queue full (oldest evicted)"});
    }
    bus.ui_command_queue.push_back(cmd);
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

    std::deque<UiCommand> queued_commands;
    queued_commands.swap(bus.ui_command_queue);

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
