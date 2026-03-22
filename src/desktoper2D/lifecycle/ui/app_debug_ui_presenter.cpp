#include "desktoper2D/lifecycle/ui/app_debug_ui_presenter.h"

#include <algorithm>

namespace desktoper2D {

namespace {
const char *TaskPrimaryCategoryNamePresenter(TaskPrimaryCategory c) {
    switch (c) {
        case TaskPrimaryCategory::Work: return "work";
        case TaskPrimaryCategory::Game: return "game";
        default: return "unknown";
    }
}

const char *TaskSecondaryCategoryNamePresenter(TaskSecondaryCategory c) {
    switch (c) {
        case TaskSecondaryCategory::WorkCoding: return "coding";
        case TaskSecondaryCategory::WorkDebugging: return "debugging";
        case TaskSecondaryCategory::WorkReadingDocs: return "reading_docs";
        case TaskSecondaryCategory::WorkMeetingNotes: return "meeting_notes";
        case TaskSecondaryCategory::GameLobby: return "lobby";
        case TaskSecondaryCategory::GameMatch: return "match";
        case TaskSecondaryCategory::GameSettlement: return "settlement";
        case TaskSecondaryCategory::GameMenu: return "menu";
        default: return "unknown";
    }
}

const char *WorkspaceModeLabel(WorkspaceMode mode) {
    switch (mode) {
        case WorkspaceMode::Animation: return "Animation";
        case WorkspaceMode::Debug: return "Debug";
        case WorkspaceMode::Perception: return "Perception";
        case WorkspaceMode::Authoring: return "Authoring";
        default: return "Animation";
    }
}
}  // namespace

RuntimeErrorDomain PickRecentErrorDomain(const AppRuntime &runtime) {
    const std::array<const RuntimeErrorInfo *, 9> infos = {
        &runtime.chat_error_info,
        &runtime.asr_error_info,
        &runtime.plugin.error_info,
        &runtime.perception_state.facemesh_error_info,
        &runtime.perception_state.ocr_error_info,
        &runtime.perception_state.scene_error_info,
        &runtime.perception_state.capture_error_info,
        &runtime.perception_state.system_context_error_info,
        &runtime.reminder_error_info,
    };
    for (const RuntimeErrorInfo *info : infos) {
        if (info->code != RuntimeErrorCode::Ok) {
            return info->domain;
        }
    }
    return RuntimeErrorDomain::None;
}

std::string PickRecentErrorText(const AppRuntime &runtime) {
    const std::array<const std::string *, 9> texts = {
        &runtime.chat_last_error,
        &runtime.asr_last_error,
        &runtime.plugin.last_error,
        &runtime.perception_state.camera_facemesh_last_error,
        &runtime.perception_state.ocr_last_error,
        &runtime.perception_state.scene_classifier_last_error,
        &runtime.perception_state.screen_capture_last_error,
        &runtime.perception_state.system_context_last_error,
        &runtime.reminder_last_error,
    };
    for (const std::string *text : texts) {
        if (text != nullptr && !text->empty()) {
            return *text;
        }
    }
    return {};
}

const char *RuntimeModuleStateName(RuntimeModuleState s) {
    switch (s) {
        case RuntimeModuleState::Ok: return "OK";
        case RuntimeModuleState::Degraded: return "DEGRADED";
        case RuntimeModuleState::Failed: return "FAILED";
        case RuntimeModuleState::Recovering: return "RECOVERING";
        default: return "UNKNOWN";
    }
}

ImVec4 RuntimeModuleStateColor(RuntimeModuleState s) {
    switch (s) {
        case RuntimeModuleState::Ok: return ImVec4(0.35f, 0.9f, 0.45f, 1.0f);
        case RuntimeModuleState::Degraded: return ImVec4(1.0f, 0.82f, 0.25f, 1.0f);
        case RuntimeModuleState::Failed: return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        case RuntimeModuleState::Recovering: return ImVec4(0.35f, 0.82f, 1.0f, 1.0f);
        default: return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    }
}

RuntimeModuleState ClassifyModuleState(const RuntimeErrorInfo &err,
                                       bool ready,
                                       bool degraded_hint,
                                       bool recovering_hint) {
    if (recovering_hint) {
        return RuntimeModuleState::Recovering;
    }
    if (degraded_hint || err.code == RuntimeErrorCode::TimeoutDegraded || err.code == RuntimeErrorCode::AutoDisabled) {
        return RuntimeModuleState::Degraded;
    }
    if (!ready) {
        return RuntimeModuleState::Failed;
    }
    if (err.code != RuntimeErrorCode::Ok) {
        return RuntimeModuleState::Failed;
    }
    return RuntimeModuleState::Ok;
}

std::vector<RuntimeErrorRow> BuildRuntimeErrorRows(const AppRuntime &runtime) {
    return {
        {"Chat",
         &runtime.chat_error_info,
         ClassifyModuleState(runtime.chat_error_info,
                             runtime.feature_flags.chat_enabled ? runtime.chat_ready : true,
                             false,
                             false),
         0},
        {"ASR",
         &runtime.asr_error_info,
         ClassifyModuleState(runtime.asr_error_info,
                             runtime.feature_flags.asr_enabled ? runtime.asr_ready : true,
                             false,
                             false),
         1},
        {"Plugin.Worker",
         &runtime.plugin.error_info,
         ClassifyModuleState(runtime.plugin.error_info,
                             runtime.plugin.ready,
                             runtime.plugin.auto_disabled || runtime.plugin.timeout_rate > 0.10,
                             (!runtime.plugin.auto_disabled && runtime.plugin.recover_count > 0 &&
                              runtime.plugin.error_info.code != RuntimeErrorCode::Ok)),
         2},
        {"Perception.FaceMesh",
         &runtime.perception_state.facemesh_error_info,
         ClassifyModuleState(runtime.perception_state.facemesh_error_info,
                             runtime.perception_state.camera_facemesh_ready,
                             runtime.face_map_sensor_fallback_active,
                             false),
         3},
        {"Perception.OCR",
         &runtime.perception_state.ocr_error_info,
         ClassifyModuleState(runtime.perception_state.ocr_error_info,
                             runtime.perception_state.ocr_ready,
                             runtime.perception_state.ocr_skipped_due_timeout,
                             false),
         4},
        {"Perception.Scene",
         &runtime.perception_state.scene_error_info,
         ClassifyModuleState(runtime.perception_state.scene_error_info,
                             runtime.perception_state.scene_classifier_ready,
                             false,
                             false),
         5},
        {"Perception.Capture",
         &runtime.perception_state.capture_error_info,
         ClassifyModuleState(runtime.perception_state.capture_error_info,
                             runtime.perception_state.screen_capture_ready,
                             false,
                             false),
         6},
        {"Perception.SystemContext",
         &runtime.perception_state.system_context_error_info,
         ClassifyModuleState(runtime.perception_state.system_context_error_info,
                             true,
                             false,
                             false),
         7},
        {"Reminder",
         &runtime.reminder_error_info,
         ClassifyModuleState(runtime.reminder_error_info,
                             runtime.reminder_ready,
                             false,
                             false),
         8},
    };
}

JsonValue BuildRuntimeSnapshotJson(const AppRuntime &runtime) {
    JsonObject root;
    root.emplace("schema", JsonValue::makeString("desktoper2D.runtime.snapshot.v1"));
    root.emplace("ts_unix_sec", JsonValue::makeNumber(static_cast<double>(std::time(nullptr))));

    JsonObject perf;
    perf.emplace("fps", JsonValue::makeNumber(runtime.debug_fps));
    perf.emplace("frame_ms", JsonValue::makeNumber(runtime.debug_frame_ms));
    root.emplace("perf", JsonValue::makeObject(std::move(perf)));

    JsonObject perception;
    perception.emplace("capture_ready", JsonValue::makeBool(runtime.perception_state.screen_capture_ready));
    perception.emplace("scene_ready", JsonValue::makeBool(runtime.perception_state.scene_classifier_ready));
    perception.emplace("ocr_ready", JsonValue::makeBool(runtime.perception_state.ocr_ready));
    perception.emplace("facemesh_ready", JsonValue::makeBool(runtime.perception_state.camera_facemesh_ready));
    perception.emplace("capture_success", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.screen_capture_success_count)));
    perception.emplace("capture_fail", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.screen_capture_fail_count)));
    perception.emplace("scene_label", JsonValue::makeString(runtime.perception_state.scene_result.label));
    perception.emplace("ocr_summary", JsonValue::makeString(runtime.perception_state.ocr_result.summary));
    root.emplace("perception", JsonValue::makeObject(std::move(perception)));

    JsonObject err_counts;
    err_counts.emplace("capture", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.capture_error_info.count)));
    err_counts.emplace("scene", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.scene_error_info.count)));
    err_counts.emplace("ocr", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.ocr_error_info.count)));
    err_counts.emplace("facemesh", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.facemesh_error_info.count)));
    err_counts.emplace("system_context", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.system_context_error_info.count)));
    err_counts.emplace("plugin", JsonValue::makeNumber(static_cast<double>(runtime.plugin.error_info.count)));
    err_counts.emplace("asr", JsonValue::makeNumber(static_cast<double>(runtime.asr_error_info.count)));
    err_counts.emplace("chat", JsonValue::makeNumber(static_cast<double>(runtime.chat_error_info.count)));
    err_counts.emplace("reminder", JsonValue::makeNumber(static_cast<double>(runtime.reminder_error_info.count)));
    root.emplace("error_counts", JsonValue::makeObject(std::move(err_counts)));

    JsonObject degraded_counts;
    degraded_counts.emplace("capture", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.capture_error_info.degraded_count)));
    degraded_counts.emplace("scene", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.scene_error_info.degraded_count)));
    degraded_counts.emplace("ocr", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.ocr_error_info.degraded_count)));
    degraded_counts.emplace("facemesh", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.facemesh_error_info.degraded_count)));
    degraded_counts.emplace("system_context", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.system_context_error_info.degraded_count)));
    degraded_counts.emplace("plugin", JsonValue::makeNumber(static_cast<double>(runtime.plugin.error_info.degraded_count)));
    degraded_counts.emplace("asr", JsonValue::makeNumber(static_cast<double>(runtime.asr_error_info.degraded_count)));
    degraded_counts.emplace("chat", JsonValue::makeNumber(static_cast<double>(runtime.chat_error_info.degraded_count)));
    degraded_counts.emplace("reminder", JsonValue::makeNumber(static_cast<double>(runtime.reminder_error_info.degraded_count)));
    root.emplace("degraded_counts", JsonValue::makeObject(std::move(degraded_counts)));

    return JsonValue::makeObject(std::move(root));
}

OverviewReadModel BuildOverviewReadModel(const AppRuntime &runtime) {
    OverviewReadModel m{};
    m.frame_ms = runtime.debug_frame_ms;
    m.fps = runtime.debug_fps;
    m.model_parts = static_cast<int>(runtime.model.parts.size());
    m.model_loaded = runtime.model_loaded;
    m.plugin_degraded = runtime.plugin.auto_disabled || runtime.plugin.timeout_rate > 0.10 || !runtime.plugin.last_error.empty();
    m.plugin_timeout_rate = runtime.plugin.timeout_rate;
    m.asr_available = runtime.feature_flags.asr_enabled && runtime.asr_ready;
    m.chat_available = runtime.feature_flags.chat_enabled && runtime.chat_ready;
    m.recent_error_domain = PickRecentErrorDomain(runtime);
    m.recent_error_text = PickRecentErrorText(runtime);
    m.task_primary = TaskPrimaryCategoryNamePresenter(runtime.task_decision.primary);
    m.task_secondary = TaskSecondaryCategoryNamePresenter(runtime.task_decision.secondary);
    return m;
}

WorkspaceReadModel BuildWorkspaceReadModel(const AppRuntime &runtime) {
    WorkspaceReadModel m{};
    m.workspace_mode = runtime.workspace_ui.mode;
    m.layout_mode = runtime.workspace_ui.panels.layout_mode;
    m.workspace_label = WorkspaceModeLabel(runtime.workspace_ui.mode);
    m.layout_label = runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual ? "Manual" : "Preset";
    m.dock_rebuild_requested = runtime.workspace_ui.dock_rebuild_requested;
    m.preset_apply_requested = runtime.workspace_ui.panels.preset_apply_requested;
    return m;
}

OpsReadModel BuildOpsReadModel(const AppRuntime &runtime, const std::string &runtime_ops_status) {
    OpsReadModel m{};
    m.running = runtime.running;
    m.runtime_ops_status = runtime_ops_status;
    return m;
}

const DefaultPluginCatalogEntry *FindDefaultPluginCatalogEntry(const AppRuntime &runtime, std::string_view name) {
    const auto it = std::find_if(runtime.plugin.default_plugin_catalog_entries.begin(),
                                 runtime.plugin.default_plugin_catalog_entries.end(),
                                 [&](const DefaultPluginCatalogEntry &entry) { return entry.name == name; });
    return it == runtime.plugin.default_plugin_catalog_entries.end() ? nullptr : &(*it);
}

}  // namespace desktoper2D
