#pragma once

#include "desktoper2D/lifecycle/state/runtime_audio_state.h"
#include "desktoper2D/lifecycle/state/runtime_window_state.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "desktoper2D/controllers/app_bootstrap.h"
#include "desktoper2D/controllers/interaction_controller.h"
#include "desktoper2D/core/model.h"
#include "desktoper2D/editor/editor_commands.h"
#include "desktoper2D/editor/editor_controller.h"
#include "desktoper2D/editor/editor_gizmo.h"
#include "desktoper2D/lifecycle/inference_adapter.h"
#include "desktoper2D/lifecycle/perception_pipeline.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"
#include "desktoper2D/lifecycle/reminder_service.h"
#include "desktoper2D/lifecycle/asr/asr_provider.h"
#include "desktoper2D/lifecycle/asr/asr_session_service.h"
#include "desktoper2D/lifecycle/asr/vad_segmenter.h"
#include "desktoper2D/lifecycle/chat/chat_provider.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/state/task_category_types.h"

namespace desktoper2D {

struct RuntimeMetricsSample {
    std::int64_t ts_ms = 0;
    std::uint64_t seq = 0;
    double capture_success_rate = 0.0;
    double scene_p95_latency_ms = 0.0;
    double ocr_p95_latency_ms = 0.0;
    double face_p95_latency_ms = 0.0;
    double ocr_timeout_rate = 0.0;
    double asr_timeout_rate = 0.0;
    double plugin_timeout_rate = 0.0;
};

struct PluginConfigEntry {
    std::string name;
    std::string config_path;
    std::string model_id;
    std::string model_version;
};

struct AsrProviderEntry {
    std::string name;
    std::string endpoint;
    std::string api_key;
    std::string model_path;
};

struct OcrModelEntry {
    std::string name;
    std::string det_path;
    std::string rec_path;
    std::string keys_path;
};

enum class UnifiedPluginKind {
    Asr,
    Facemesh,
    SceneClassifier,
    Ocr,
    BehaviorUser,
};

enum class UnifiedPluginStatus {
    NotLoaded,
    Loading,
    Ready,
    Error,
    Disabled,
};

struct UnifiedPluginEntry {
    std::string id;
    UnifiedPluginKind kind = UnifiedPluginKind::BehaviorUser;
    std::string name;
    std::string version;
    std::string source;
    std::vector<std::string> assets;
    std::string backend;
    UnifiedPluginStatus status = UnifiedPluginStatus::NotLoaded;
    std::string last_error;
};

enum class PluginLogLevel {
    Info,
    Warning,
    Error,
};

struct PluginLogEntry {
    std::int64_t ts_ms = 0;
    PluginLogLevel level = PluginLogLevel::Info;
    std::string message;
    int error_code = 0;
};

struct PluginAssetOverride {
    std::string onnx;
    std::string labels;
    std::string keys;
    std::vector<std::string> extra_onnx;
};

struct UserPluginCreateRequest {
    std::string name;
    std::string template_path = "assets/plugin_behavior_config.json";
};

struct RuntimeFeatureFlags {
    bool scene_classifier_enabled = true;
    bool ocr_enabled = true;
    bool face_emotion_enabled = true;
    bool face_param_mapping_enabled = true;
    bool asr_enabled = false;
    bool plugin_enabled = true;
    bool chat_enabled = true;
};

enum class AxisConstraint {
    None,
    XOnly,
    YOnly,
};

enum class WorkspaceMode {
    Animation,
    Debug,
    Perception,
    Authoring,
};

enum class WorkspaceLayoutMode {
    Preset,
    Manual,
};

enum class UiCommandType {
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
    ToggleInspectorWindow,
    ToggleReminderWindow,
    TogglePluginQuickControlWindow,
    ForceDockRebuild,
    ResetPerceptionState,
    ResetErrorCounters,
    ExportRuntimeSnapshot,
    TriggerSingleStepSampling,
    CloseProgram,
};

struct UiCommand {
    UiCommandType type = UiCommandType::SwitchWorkspaceMode;
    int int_value = 0;
    bool bool_value = false;
};

enum class RuntimeEventType {
    UiCommandQueued,
    UiCommandDroppedQueueFull,
    UiCommandConsumed,
    OpsStatusUpdated,
    ProgramCloseRequested,
};

struct RuntimeEvent {
    RuntimeEventType type = RuntimeEventType::UiCommandQueued;
    UiCommandType command_type = UiCommandType::SwitchWorkspaceMode;
    int int_value = 0;
    bool bool_value = false;
    std::string message;
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

struct RuntimeCommandBus {
    std::vector<UiCommand> ui_command_queue;
    std::size_t ui_command_queue_capacity = 512;
    std::deque<RuntimeEvent> runtime_event_queue;
    std::size_t runtime_event_queue_capacity = 1024;
};

struct AppRuntime {
    RuntimeWindowState window_state{};
    RuntimeAudioState audio_state{};

    ModelRuntime model;
    bool model_loaded = false;
    float model_time = 0.0f;

    bool running = true;
    std::string runtime_ops_status;

    RuntimeCommandBus command_bus{};

    bool dev_hot_reload_enabled = true;
    float hot_reload_poll_accum_sec = 0.0f;
    std::filesystem::file_time_type model_last_write_time{};
    bool model_last_write_time_valid = false;

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

    // 编辑器画布视图（pan/zoom），不直接修改模型变换数据。
    float editor_view_pan_x = 0.0f;
    float editor_view_pan_y = 0.0f;
    float editor_view_zoom = 1.0f;
    bool editor_view_dragging = false;
    float editor_view_drag_last_x = 0.0f;
    float editor_view_drag_last_y = 0.0f;

    AxisConstraint axis_constraint = AxisConstraint::None;
    bool snap_enabled = false;
    float snap_grid = 10.0f;

    bool dragging_model_whole = false;
    float dragging_model_last_x = 0.0f;
    float dragging_model_last_y = 0.0f;

    bool property_panel_enabled = true;
    int selected_editor_prop = 0;

    // 资源树 + Inspector 联动
    char resource_tree_filter[128] = "";
    bool resource_tree_auto_expand_matches = true;
    int selected_deformer_type = 0; // 0=Warp, 1=Rotation

    struct WindowLayoutState {
        float pos_x = 0.0f;
        float pos_y = 0.0f;
        float size_w = 0.0f;
        float size_h = 0.0f;
        bool collapsed = false;
        bool initialized = false;
    };

     struct WorkspaceState {
         bool show_workspace_window = true;
         bool show_overview_window = true;
         bool show_editor_window = true;
         bool show_timeline_window = true;
         bool show_perception_window = true;
         bool show_mapping_window = true;
         bool show_asr_chat_window = true;
         bool show_error_window = true;
         bool show_inspector_window = true;
         bool show_reminder_window = true;
         bool show_plugin_quick_control_window = true;
         std::string manual_docking_ini;
         WorkspaceLayoutMode layout_mode = WorkspaceLayoutMode::Preset;
         WorkspaceMode last_applied_mode = WorkspaceMode::Debug;
         bool preset_apply_requested = false;
         bool manual_layout_reset_requested = false;
         bool manual_layout_pending_load = false;
         bool manual_layout_save_suppressed = false;
         int manual_layout_stable_frames = 0;
     };
WindowLayoutState runtime_debug_window_layout{};

    SDL_Window *&window = window_state.window;
    SDL_Renderer *&renderer = window_state.renderer;
    SDL_Tray *&tray = window_state.tray;
    SDL_TrayEntry *&entry_click_through = window_state.entry_click_through;
    SDL_TrayEntry *&entry_show_hide = window_state.entry_show_hide;
    SDL_Texture *&demo_texture = window_state.demo_texture;
    int &demo_texture_w = window_state.demo_texture_w;
    int &demo_texture_h = window_state.demo_texture_h;
    bool &click_through = window_state.click_through;
    bool &window_visible = window_state.window_visible;
    int &window_w = window_state.window_w;
    int &window_h = window_state.window_h;
    SDL_Rect &interactive_rect = window_state.interactive_rect;

    SDL_AudioDeviceID &mic_device_id = audio_state.mic_device_id;
    SDL_AudioSpec &mic_obtained_spec = audio_state.mic_obtained_spec;
    std::mutex &mic_mutex = audio_state.mic_mutex;
    std::deque<float> &mic_pcm_queue = audio_state.mic_pcm_queue;
    WindowLayoutState inspector_window_layout{};
    WindowLayoutState reminder_window_layout{};
    WorkspaceState workspace{};

    // 兼容层：逐步迁移到 workspace.* 后可删除以下别名字段。
    bool &show_workspace_window = workspace.show_workspace_window;
    bool &show_overview_window = workspace.show_overview_window;
    bool &show_editor_window = workspace.show_editor_window;
    bool &show_timeline_window = workspace.show_timeline_window;
    bool &show_perception_window = workspace.show_perception_window;
    bool &show_mapping_window = workspace.show_mapping_window;
     bool &show_asr_chat_window = workspace.show_asr_chat_window;
     bool &show_error_window = workspace.show_error_window;
     bool &show_inspector_window = workspace.show_inspector_window;
     bool &show_reminder_window = workspace.show_reminder_window;
     bool &show_plugin_quick_control_window = workspace.show_plugin_quick_control_window;
std::string &workspace_manual_docking_ini = workspace.manual_docking_ini;
    WorkspaceLayoutMode &workspace_layout_mode = workspace.layout_mode;
    WorkspaceMode &last_applied_workspace_mode = workspace.last_applied_mode;
    bool &workspace_preset_apply_requested = workspace.preset_apply_requested;
    bool &workspace_manual_layout_reset_requested = workspace.manual_layout_reset_requested;
    bool &workspace_manual_layout_pending_load = workspace.manual_layout_pending_load;
    bool &workspace_manual_layout_save_suppressed = workspace.manual_layout_save_suppressed;
    int &workspace_manual_layout_stable_frames = workspace.manual_layout_stable_frames;

    // 参数面板增强：分组、搜索与批量绑定（UI 状态）
    int param_group_mode = 0; // 0=prefix, 1=semantic
    int selected_param_group_index = 0;
    char param_search[128] = "";
    int batch_bind_prop_type = 0; // 对应 BindingType 枚举值
    float batch_bind_in_min = -1.0f;
    float batch_bind_in_max = 1.0f;
    float batch_bind_out_min = -1.0f;
    float batch_bind_out_max = 1.0f;

    // 时间轴 v1
    bool timeline_enabled = false;
    int timeline_selected_channel_index = 0;
    float timeline_cursor_sec = 0.0f;
    float timeline_duration_sec = 3.0f;
    bool timeline_snap_enabled = true;
    int timeline_snap_mode = 0; // 0=整数帧, 1=0.1s, 2=播放头
    float timeline_snap_fps = 30.0f;
    std::vector<int> timeline_selected_keyframe_indices;
    std::vector<std::uint64_t> timeline_selected_keyframe_ids;
    std::vector<TimelineKeyframe> timeline_keyframe_clipboard;
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

    // 工程级会话文件（project.json）路径
    std::string current_project_path = "assets/project.json";
    bool editor_project_dirty = false;

    float debug_fps = 0.0f;
    float debug_frame_ms = 0.0f;
    float debug_fps_accum_sec = 0.0f;
    int debug_fps_accum_frames = 0;

    bool gui_enabled = true;
    WorkspaceMode workspace_mode = WorkspaceMode::Debug;
    bool workspace_dock_rebuild_requested = false;

    InteractionControllerState interaction_state{};

    std::unique_ptr<IInferenceAdapter> inference_adapter;
    bool plugin_ready = false;
    PluginParamBlendMode plugin_param_blend_mode = PluginParamBlendMode::Override;
    BehaviorFusionConfig behavior_fusion_config{};

    std::vector<PluginConfigEntry> plugin_config_entries;
    int plugin_selected_entry_index = -1;
    bool plugin_config_refresh_requested = false;
    std::string plugin_config_scan_error;
    char plugin_name_input[128] = "";
    std::string plugin_switch_status;
    std::string plugin_switch_error;
    std::string plugin_delete_status;
    std::string plugin_delete_error;

    std::vector<UnifiedPluginEntry> unified_plugin_entries;
    int unified_plugin_selected_index = -1;
    bool unified_plugin_refresh_requested = false;
    std::string unified_plugin_scan_error;
    char unified_plugin_name_input[128] = "";
    char unified_plugin_new_name_input[128] = "";
    std::string unified_plugin_switch_status;
    std::string unified_plugin_switch_error;
    std::string unified_plugin_create_status;
    std::string unified_plugin_create_error;
    std::string unified_plugin_delete_status;
    std::string unified_plugin_delete_error;

    std::vector<AsrProviderEntry> asr_provider_entries;
    int asr_selected_entry_index = -1;
    char asr_provider_input[128] = "";
    std::string asr_switch_status;
    std::string asr_switch_error;
    std::string asr_current_provider_name;

    char override_asr_model_input[260] = "";
    char override_scene_model_input[260] = "";
    char override_scene_labels_input[260] = "";
    char override_facemesh_model_input[260] = "";
    char override_facemesh_labels_input[260] = "";
    char override_ocr_det_input[260] = "";
    char override_ocr_rec_input[260] = "";
    char override_ocr_keys_input[260] = "";
    char override_behavior_onnx_input[260] = "";
    char override_behavior_extra_onnx_input[260] = "";

    std::string override_asr_model_path;
    std::string override_scene_model_path;
    std::string override_scene_labels_path;
    std::string override_facemesh_model_path;
    std::string override_facemesh_labels_path;
    std::string override_ocr_det_path;
    std::string override_ocr_rec_path;
    std::string override_ocr_keys_path;
    std::string override_behavior_onnx_path;
    std::string override_behavior_extra_onnx_csv;
    std::string override_apply_status;
    std::string override_apply_error;

    std::vector<OcrModelEntry> ocr_model_entries;
    int ocr_selected_entry_index = -1;
    char ocr_model_input[128] = "";
    std::string ocr_switch_status;
    std::string ocr_switch_error;

    std::unordered_map<std::string, std::deque<PluginLogEntry>> plugin_logs;

    ReminderService reminder_service;
    bool reminder_ready = false;
    float reminder_poll_accum_sec = 0.0f;
    char reminder_title_input[128] = "喝水";
    int reminder_after_min = 10;
    char reminder_search[128] = "";
    int reminder_filter_mode = 0; // 0=all,1=upcoming,2=overdue
    int reminder_sort_mode = 0;   // 0=due asc,1=due desc,2=overdue first
    int reminder_page = 0;
    std::vector<ReminderItem> reminder_upcoming;
    std::string reminder_last_error;

    PerceptionPipeline perception_pipeline;
    PerceptionPipelineState perception_state;

    RuntimeFeatureFlags feature_flags{};
    bool &feature_scene_classifier_enabled = feature_flags.scene_classifier_enabled;
    bool &feature_ocr_enabled = feature_flags.ocr_enabled;
    bool &feature_face_emotion_enabled = feature_flags.face_emotion_enabled;
    bool &feature_face_param_mapping_enabled = feature_flags.face_param_mapping_enabled;
    bool &feature_asr_enabled = feature_flags.asr_enabled;
    bool &feature_plugin_enabled = feature_flags.plugin_enabled;
    bool &feature_chat_enabled = feature_flags.chat_enabled;

    bool runtime_observability_log_enabled = true;
    float runtime_observability_log_interval_sec = 3.0f;
    float runtime_observability_log_accum_sec = 0.0f;

    std::vector<double> scene_latency_window_ms;
    std::vector<double> ocr_latency_window_ms;
    std::vector<double> face_latency_window_ms;
    std::size_t scene_latency_ring_head = 0;
    std::size_t scene_latency_ring_size = 0;
    std::size_t ocr_latency_ring_head = 0;
    std::size_t ocr_latency_ring_size = 0;
    std::size_t face_latency_ring_head = 0;
    std::size_t face_latency_ring_size = 0;
    double scene_p95_latency_ms = 0.0;
    double ocr_p95_latency_ms = 0.0;
    double face_p95_latency_ms = 0.0;
    std::size_t runtime_metrics_window_size = 120;

    std::vector<RuntimeMetricsSample> runtime_metrics_series;
    std::size_t runtime_metrics_series_head = 0;
    std::size_t runtime_metrics_series_size = 0;
    std::size_t runtime_metrics_series_capacity = 2048;
    std::uint64_t runtime_metrics_seq = 0;

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
    // ASR 会话流缓存（短期上下文）
    AsrSessionState asr_session_state{};

    std::unique_ptr<IChatProvider> chat_provider;
    bool chat_ready = false;
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
    double plugin_last_latency_ms = 0.0;
    double plugin_avg_latency_ms = 0.0;
    double plugin_latency_p50_ms = 0.0;
    double plugin_latency_p95_ms = 0.0;
    double plugin_success_rate = 0.0;
    int plugin_current_update_hz = 60;
    bool plugin_auto_disabled = false;
    std::string plugin_last_error;
    std::string plugin_route_selected = "unknown";
    double plugin_route_scene_score = 0.0;
    double plugin_route_task_score = 0.0;
    double plugin_route_presence_score = 0.0;
    double plugin_route_total_score = 0.0;
    std::vector<std::string> plugin_route_rejected_summary;

    RuntimeErrorInfo plugin_error_info{};
    RuntimeErrorInfo asr_error_info{};
    RuntimeErrorInfo decision_error_info{};
    RuntimeErrorInfo chat_error_info{};
    RuntimeErrorInfo reminder_error_info{};

    TaskPrimaryCategory task_primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory task_secondary = TaskSecondaryCategory::Unknown;
    TaskCategoryConfig task_category_config{};
};

using RuntimeToViewFn = std::function<void(const AppRuntime &, float world_x, float world_y, float *out_view_x, float *out_view_y)>;
using ViewToRuntimeFn = std::function<void(const AppRuntime &, float view_x, float view_y, float *out_world_x, float *out_world_y)>;

extern AppRuntime g_runtime;
extern EditorControllerState g_editor_state;
extern RuntimeToViewFn g_runtime_to_view;
extern ViewToRuntimeFn g_view_to_runtime;

}  // namespace desktoper2D
