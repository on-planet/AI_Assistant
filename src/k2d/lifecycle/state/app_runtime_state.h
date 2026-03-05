#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "k2d/controllers/app_bootstrap.h"
#include "k2d/controllers/interaction_controller.h"
#include "k2d/core/model.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/editor/editor_controller.h"
#include "k2d/editor/editor_gizmo.h"
#include "k2d/lifecycle/inference_adapter.h"
#include "k2d/lifecycle/perception_pipeline.h"
#include "k2d/lifecycle/plugin_lifecycle.h"
#include "k2d/lifecycle/reminder_service.h"
#include "k2d/lifecycle/asr/asr_provider.h"
#include "k2d/lifecycle/asr/vad_segmenter.h"
#include "k2d/lifecycle/chat/chat_provider.h"
#include "k2d/lifecycle/observability/runtime_error_codes.h"
#include "k2d/lifecycle/state/task_category_types.h"

namespace k2d {

enum class AxisConstraint {
    None,
    XOnly,
    YOnly,
};

enum class EditorProp {
    PosX,
    PosY,
    PivotX,
    PivotY,
    RotDeg,
    ScaleX,
    ScaleY,
    Count,
};

struct AppRuntime {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Tray *tray = nullptr;

    SDL_AudioDeviceID mic_device_id = 0;
    SDL_AudioSpec mic_obtained_spec{};
    std::mutex mic_mutex;
    std::deque<float> mic_pcm_queue;
    SDL_TrayEntry *entry_click_through = nullptr;
    SDL_TrayEntry *entry_show_hide = nullptr;

    SDL_Texture *demo_texture = nullptr;
    int demo_texture_w = 0;
    int demo_texture_h = 0;

    ModelRuntime model;
    bool model_loaded = false;
    float model_time = 0.0f;

    bool running = true;

    bool dev_hot_reload_enabled = true;
    float hot_reload_poll_accum_sec = 0.0f;
    std::filesystem::file_time_type model_last_write_time{};
    bool model_last_write_time_valid = false;
    bool click_through = false;
    bool window_visible = true;
    int window_w = 0;
    int window_h = 0;
    SDL_Rect interactive_rect{0, 0, 0, 0};

    bool show_debug_stats = true;
    bool manual_param_mode = false;
    int selected_param_index = 0;

    bool edit_mode = false;
    int selected_part_index = -1;
    bool dragging_part = false;
    bool dragging_pivot = false;
    float drag_last_x = 0.0f;
    float drag_last_y = 0.0f;
    float drag_start_pos_x = 0.0f;
    float drag_start_pos_y = 0.0f;
    float drag_start_pivot_x = 0.0f;
    float drag_start_pivot_y = 0.0f;

    AxisConstraint axis_constraint = AxisConstraint::None;
    bool snap_enabled = false;
    float snap_grid = 10.0f;

    bool dragging_model_whole = false;
    float dragging_model_last_x = 0.0f;
    float dragging_model_last_y = 0.0f;

    bool property_panel_enabled = true;
    int selected_editor_prop = 0;

    std::vector<EditCommand> undo_stack;
    std::vector<EditCommand> redo_stack;

    GizmoHandle gizmo_hover_handle = GizmoHandle::None;
    GizmoHandle gizmo_active_handle = GizmoHandle::None;
    bool gizmo_dragging = false;
    float gizmo_drag_start_mouse_x = 0.0f;
    float gizmo_drag_start_mouse_y = 0.0f;
    float gizmo_drag_start_pos_x = 0.0f;
    float gizmo_drag_start_pos_y = 0.0f;
    float gizmo_drag_start_rot_deg = 0.0f;
    float gizmo_drag_start_scale_x = 1.0f;
    float gizmo_drag_start_scale_y = 1.0f;
    float gizmo_drag_start_angle = 0.0f;
    float gizmo_drag_start_dist = 1.0f;

    bool edit_capture_active = false;
    EditCommand active_edit_cmd;

    std::string editor_status;
    float editor_status_ttl = 0.0f;

    float debug_fps = 0.0f;
    float debug_frame_ms = 0.0f;
    float debug_fps_accum_sec = 0.0f;
    int debug_fps_accum_frames = 0;

    bool gui_enabled = true;

    InteractionControllerState interaction_state{};

    std::unique_ptr<IInferenceAdapter> inference_adapter;
    bool plugin_ready = false;
    PluginParamBlendMode plugin_param_blend_mode = PluginParamBlendMode::Override;

    ReminderService reminder_service;
    bool reminder_ready = false;
    float reminder_poll_accum_sec = 0.0f;
    char reminder_title_input[128] = "喝水";
    int reminder_after_min = 10;
    std::vector<ReminderItem> reminder_upcoming;
    std::string reminder_last_error;

    PerceptionPipeline perception_pipeline;
    PerceptionPipelineState perception_state;

    bool feature_scene_classifier_enabled = true;
    bool feature_ocr_enabled = true;
    bool feature_face_emotion_enabled = true;
    bool feature_face_param_mapping_enabled = true;
    bool feature_asr_enabled = false;

    bool runtime_observability_log_enabled = true;
    float runtime_observability_log_interval_sec = 3.0f;
    float runtime_observability_log_accum_sec = 0.0f;

    float face_map_min_confidence = 0.45f;
    float face_map_head_pose_deadzone_deg = 2.0f;
    float face_map_yaw_max_deg = 25.0f;
    float face_map_pitch_max_deg = 18.0f;
    float face_map_eye_open_threshold = 0.25f;
    float face_map_param_weight = 0.65f;
    float face_map_smooth_alpha = 0.35f;

    // 传感器异常时的 fallback 姿态模板（归一化参数空间）
    bool face_map_sensor_fallback_enabled = true;
    float face_map_sensor_fallback_head_yaw = 0.0f;
    float face_map_sensor_fallback_head_pitch = -0.08f;
    float face_map_sensor_fallback_eye_open = 0.38f;
    float face_map_sensor_fallback_weight = 1.0f;

    std::string face_map_gate_reason = "init";
    std::string face_map_sensor_fallback_reason = "none";
    bool face_map_sensor_fallback_active = false;
    float face_map_raw_yaw_deg = 0.0f;
    float face_map_raw_pitch_deg = 0.0f;
    float face_map_raw_eye_open = 0.0f;
    float face_map_out_head_yaw = 0.0f;
    float face_map_out_head_pitch = 0.0f;
    float face_map_out_eye_open = 0.0f;

    std::unique_ptr<IAsrProvider> asr_provider;
    bool asr_ready = false;
    float asr_poll_accum_sec = 0.0f;
    std::string asr_last_error;
    AsrRecognitionResult asr_last_result;

    std::unique_ptr<IChatProvider> chat_provider;
    bool chat_ready = false;
    bool feature_chat_enabled = true;
    bool prefer_cloud_chat = true;
    std::string chat_last_error;
    std::string chat_last_switch_reason;
    char chat_input[512] = "Hello, introduce yourself";
    std::string chat_last_answer;

    EnergyVadSegmenter asr_vad;
    std::vector<float> asr_audio_buffer;
    int asr_frame_samples = 320; // 20ms @ 16k

    // ASR 监控指标
    std::int64_t asr_total_segments = 0;
    std::int64_t asr_timeout_segments = 0;
    std::int64_t asr_cloud_attempts = 0;
    std::int64_t asr_cloud_success = 0;
    std::int64_t asr_cloud_fallbacks = 0;
    double asr_audio_total_sec = 0.0;
    double asr_infer_total_sec = 0.0;
    double asr_rtf = 0.0;
    double asr_timeout_rate = 0.0;
    double asr_cloud_call_ratio = 0.0;
    double asr_cloud_success_ratio = 0.0;
    double asr_wer_proxy = 0.0;
    std::string asr_last_switch_reason;

    std::uint64_t plugin_total_update_count = 0;
    std::uint64_t plugin_timeout_count = 0;
    std::uint64_t plugin_exception_count = 0;
    std::uint64_t plugin_internal_error_count = 0;
    std::uint64_t plugin_disable_count = 0;
    std::uint64_t plugin_recover_count = 0;
    double plugin_timeout_rate = 0.0;
    int plugin_current_update_hz = 60;
    bool plugin_auto_disabled = false;
    std::string plugin_last_error;

    RuntimeErrorInfo plugin_error_info{};
    RuntimeErrorInfo asr_error_info{};
    RuntimeErrorInfo chat_error_info{};
    RuntimeErrorInfo reminder_error_info{};

    TaskPrimaryCategory task_primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory task_secondary = TaskSecondaryCategory::Unknown;
    TaskCategoryConfig task_category_config{};
};

extern AppRuntime g_runtime;
extern EditorControllerState g_editor_state;

}  // namespace k2d
