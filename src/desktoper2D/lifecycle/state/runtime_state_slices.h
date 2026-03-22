#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

struct EditorStateSlice {
    bool &model_loaded;
    ModelRuntime &model;
    float &model_time;
    InteractionControllerState &interaction_state;

    bool &editor_autosave_recovery_checked;
    bool &editor_autosave_enabled;
    bool &editor_project_dirty;
    float &editor_autosave_accum_sec;
    float &editor_autosave_interval_sec;
    std::string &editor_status;
    float &editor_status_ttl;
};

struct PerceptionStateSlice {
    PerceptionPipeline &pipeline;
    PerceptionPipelineState &pipeline_state;
    RuntimeFeatureFlags &feature_flags;
    RuntimeWindowState &window_state;

    bool &model_loaded;
    ModelRuntime &model;

    float &face_map_min_confidence;
    float &face_map_head_pose_deadzone_deg;
    float &face_map_yaw_max_deg;
    float &face_map_pitch_max_deg;
    float &face_map_eye_open_threshold;
    float &face_map_param_weight;
    float &face_map_smooth_alpha;

    bool &face_map_sensor_fallback_enabled;
    float &face_map_sensor_fallback_head_yaw;
    float &face_map_sensor_fallback_head_pitch;
    float &face_map_sensor_fallback_eye_open;
    float &face_map_sensor_fallback_weight;

    std::string &face_map_gate_reason;
    std::string &face_map_sensor_fallback_reason;
    bool &face_map_sensor_fallback_active;
    float &face_map_raw_yaw_deg;
    float &face_map_raw_pitch_deg;
    float &face_map_raw_eye_open;
    float &face_map_out_head_yaw;
    float &face_map_out_head_pitch;
    float &face_map_out_eye_open;
};

struct PluginStateSlice {
    PluginRuntimeState &plugin_runtime;
    TaskDecisionState &task_decision;
    RuntimeFeatureFlags &feature_flags;
    PerceptionPipelineState &perception_state;
    RuntimeWindowState &window_state;

    bool &manual_param_mode;
    bool &show_debug_stats;
    bool &model_loaded;
    float &model_time;
};

struct WorkspaceStateSlice {
    RuntimeCommandBus &command_bus;
    WorkspaceRuntimeState &workspace;
    std::string &runtime_ops_status;
    bool &running;
};

struct OpsStateSlice {
    RuntimeAudioState &audio_state;
    RuntimeWindowState &window_state;
    RuntimeFeatureFlags &feature_flags;

    ReminderService &reminder_service;
    bool &reminder_ready;
    float &reminder_poll_accum_sec;
    std::vector<ReminderItem> &reminder_upcoming;
    std::string &reminder_last_error;

    std::unique_ptr<IAsrProvider> &asr_provider;
    std::mutex &asr_provider_mutex;
    std::uint64_t &asr_provider_generation;
    bool &asr_ready;
    float &asr_poll_accum_sec;
    std::string &asr_last_error;
    AsrRecognitionResult &asr_last_result;
    AsrSessionState &asr_session_state;
    EnergyVadSegmenter &asr_vad;
    std::vector<float> &asr_audio_buffer;
    std::size_t &asr_audio_buffer_capacity;
    int &asr_frame_samples;
    AsrAsyncRuntimeState &asr_async_state;

    std::int64_t &asr_total_segments;
    std::int64_t &asr_timeout_segments;
    std::int64_t &asr_cloud_attempts;
    std::int64_t &asr_cloud_success;
    std::int64_t &asr_cloud_fallbacks;
    double &asr_audio_total_sec;
    double &asr_infer_total_sec;
    double &asr_rtf;
    double &asr_timeout_rate;
    double &asr_cloud_call_ratio;
    double &asr_cloud_success_ratio;
    double &asr_wer_proxy;
    std::string &asr_last_switch_reason;

    RuntimeObservabilityState &observability;
    RuntimeErrorInfo &asr_error_info;
    RuntimeErrorInfo &chat_error_info;
    RuntimeErrorInfo &reminder_error_info;
};

struct RuntimeTickStateSlices {
    EditorStateSlice editor;
    PerceptionStateSlice perception;
    PluginStateSlice plugin;
    WorkspaceStateSlice workspace;
    OpsStateSlice ops;
};

inline EditorStateSlice BuildEditorStateSlice(AppRuntime &runtime) {
    return EditorStateSlice{
        .model_loaded = runtime.model_loaded,
        .model = runtime.model,
        .model_time = runtime.model_time,
        .interaction_state = runtime.interaction_state,
        .editor_autosave_recovery_checked = runtime.editor_autosave_recovery_checked,
        .editor_autosave_enabled = runtime.editor_autosave_enabled,
        .editor_project_dirty = runtime.editor_project_dirty,
        .editor_autosave_accum_sec = runtime.editor_autosave_accum_sec,
        .editor_autosave_interval_sec = runtime.editor_autosave_interval_sec,
        .editor_status = runtime.editor_status,
        .editor_status_ttl = runtime.editor_status_ttl,
    };
}

inline PerceptionStateSlice BuildPerceptionStateSlice(AppRuntime &runtime) {
    return PerceptionStateSlice{
        .pipeline = runtime.perception_pipeline,
        .pipeline_state = runtime.perception_state,
        .feature_flags = runtime.feature_flags,
        .window_state = runtime.window_state,
        .model_loaded = runtime.model_loaded,
        .model = runtime.model,
        .face_map_min_confidence = runtime.face_map_min_confidence,
        .face_map_head_pose_deadzone_deg = runtime.face_map_head_pose_deadzone_deg,
        .face_map_yaw_max_deg = runtime.face_map_yaw_max_deg,
        .face_map_pitch_max_deg = runtime.face_map_pitch_max_deg,
        .face_map_eye_open_threshold = runtime.face_map_eye_open_threshold,
        .face_map_param_weight = runtime.face_map_param_weight,
        .face_map_smooth_alpha = runtime.face_map_smooth_alpha,
        .face_map_sensor_fallback_enabled = runtime.face_map_sensor_fallback_enabled,
        .face_map_sensor_fallback_head_yaw = runtime.face_map_sensor_fallback_head_yaw,
        .face_map_sensor_fallback_head_pitch = runtime.face_map_sensor_fallback_head_pitch,
        .face_map_sensor_fallback_eye_open = runtime.face_map_sensor_fallback_eye_open,
        .face_map_sensor_fallback_weight = runtime.face_map_sensor_fallback_weight,
        .face_map_gate_reason = runtime.face_map_gate_reason,
        .face_map_sensor_fallback_reason = runtime.face_map_sensor_fallback_reason,
        .face_map_sensor_fallback_active = runtime.face_map_sensor_fallback_active,
        .face_map_raw_yaw_deg = runtime.face_map_raw_yaw_deg,
        .face_map_raw_pitch_deg = runtime.face_map_raw_pitch_deg,
        .face_map_raw_eye_open = runtime.face_map_raw_eye_open,
        .face_map_out_head_yaw = runtime.face_map_out_head_yaw,
        .face_map_out_head_pitch = runtime.face_map_out_head_pitch,
        .face_map_out_eye_open = runtime.face_map_out_eye_open,
    };
}

inline PluginStateSlice BuildPluginStateSlice(AppRuntime &runtime) {
    return PluginStateSlice{
        .plugin_runtime = runtime.plugin,
        .task_decision = runtime.task_decision,
        .feature_flags = runtime.feature_flags,
        .perception_state = runtime.perception_state,
        .window_state = runtime.window_state,
        .manual_param_mode = runtime.manual_param_mode,
        .show_debug_stats = runtime.show_debug_stats,
        .model_loaded = runtime.model_loaded,
        .model_time = runtime.model_time,
    };
}

inline WorkspaceStateSlice BuildWorkspaceStateSlice(AppRuntime &runtime) {
    return WorkspaceStateSlice{
        .command_bus = runtime.command_bus,
        .workspace = runtime.workspace_ui,
        .runtime_ops_status = runtime.runtime_ops_status,
        .running = runtime.running,
    };
}

inline OpsStateSlice BuildOpsStateSlice(AppRuntime &runtime) {
    return OpsStateSlice{
        .audio_state = runtime.audio_state,
        .window_state = runtime.window_state,
        .feature_flags = runtime.feature_flags,
        .reminder_service = runtime.reminder_service,
        .reminder_ready = runtime.reminder_ready,
        .reminder_poll_accum_sec = runtime.reminder_poll_accum_sec,
        .reminder_upcoming = runtime.reminder_upcoming,
        .reminder_last_error = runtime.reminder_last_error,
        .asr_provider = runtime.asr_provider,
        .asr_provider_mutex = runtime.asr_provider_mutex,
        .asr_provider_generation = runtime.asr_provider_generation,
        .asr_ready = runtime.asr_ready,
        .asr_poll_accum_sec = runtime.asr_poll_accum_sec,
        .asr_last_error = runtime.asr_last_error,
        .asr_last_result = runtime.asr_last_result,
        .asr_session_state = runtime.asr_session_state,
        .asr_vad = runtime.asr_vad,
        .asr_audio_buffer = runtime.asr_audio_buffer,
        .asr_audio_buffer_capacity = runtime.asr_audio_buffer_capacity,
        .asr_frame_samples = runtime.asr_frame_samples,
        .asr_async_state = runtime.asr_async_state,
        .asr_total_segments = runtime.asr_total_segments,
        .asr_timeout_segments = runtime.asr_timeout_segments,
        .asr_cloud_attempts = runtime.asr_cloud_attempts,
        .asr_cloud_success = runtime.asr_cloud_success,
        .asr_cloud_fallbacks = runtime.asr_cloud_fallbacks,
        .asr_audio_total_sec = runtime.asr_audio_total_sec,
        .asr_infer_total_sec = runtime.asr_infer_total_sec,
        .asr_rtf = runtime.asr_rtf,
        .asr_timeout_rate = runtime.asr_timeout_rate,
        .asr_cloud_call_ratio = runtime.asr_cloud_call_ratio,
        .asr_cloud_success_ratio = runtime.asr_cloud_success_ratio,
        .asr_wer_proxy = runtime.asr_wer_proxy,
        .asr_last_switch_reason = runtime.asr_last_switch_reason,
        .observability = runtime.observability,
        .asr_error_info = runtime.asr_error_info,
        .chat_error_info = runtime.chat_error_info,
        .reminder_error_info = runtime.reminder_error_info,
    };
}

inline RuntimeTickStateSlices BuildRuntimeTickStateSlices(AppRuntime &runtime) {
    return RuntimeTickStateSlices{
        .editor = BuildEditorStateSlice(runtime),
        .perception = BuildPerceptionStateSlice(runtime),
        .plugin = BuildPluginStateSlice(runtime),
        .workspace = BuildWorkspaceStateSlice(runtime),
        .ops = BuildOpsStateSlice(runtime),
    };
}

}  // namespace desktoper2D
