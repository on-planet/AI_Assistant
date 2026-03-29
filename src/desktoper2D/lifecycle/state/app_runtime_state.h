#pragma once

#include "desktoper2D/lifecycle/state/runtime_audio_state.h"
#include "desktoper2D/lifecycle/state/runtime_window_state.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "desktoper2D/controllers/app_bootstrap.h"
#include "desktoper2D/controllers/interaction_controller.h"
#include "desktoper2D/core/model.h"
#include "desktoper2D/editor/editor_commands.h"
#include "desktoper2D/editor/editor_controller.h"
#include "desktoper2D/editor/editor_gizmo.h"
#include "desktoper2D/editor/editor_types.h"
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
    bool enabled = true;
};

struct PluginConfigCacheEntry {
    PluginConfigEntry entry;
    std::filesystem::file_time_type last_write_time{};
    bool last_write_time_valid = false;
    bool parse_ok = false;
    std::string parse_error;
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

struct DefaultPluginCatalogEntry {
    std::string name;
    std::string kind;
    std::string onnx;
    std::string labels;
    std::string keys;
    std::string det;
    std::string rec;
    std::string model;
    std::string source;
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

enum class PluginDetailKind {
    None,
    Behavior,
    Asr,
    Ocr,
    Scene,
    Facemesh,
    Chat,
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
    std::string template_path;
    bool use_folder_layout = true;
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
    ToggleOcrWindow,
    ToggleAsrChatWindow,
    TogglePluginWorkerWindow,
    ToggleChatWindow,
    ToggleErrorWindow,
    ToggleInspectorWindow,
    ToggleReminderWindow,
    TogglePluginQuickControlWindow,
    ForceDockRebuild,
    ResetPerceptionState,
    ResetErrorCounters,
    ExportRuntimeSnapshot,
    TriggerSingleStepSampling,
    RefreshPluginConfigs,
    SwitchPluginByName,
    DeletePluginConfig,
    RefreshAsrProviders,
    SwitchAsrProviderByName,
    RefreshOcrModels,
    SwitchOcrModelByName,
    RefreshUnifiedPlugins,
    ApplyOverrideModels,
    ReplaceUnifiedPluginAssets,
    CreateUserPlugin,
    CloseProgram,
};

struct UiCommand {
    UiCommandType type = UiCommandType::SwitchWorkspaceMode;
    int int_value = 0;
    bool bool_value = false;
    std::string text_value;
    std::string text_value_2;
    std::string text_value_3;
    std::string text_value_4;
    std::vector<std::string> text_list_value;
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
    std::deque<UiCommand> ui_command_queue;
    std::size_t ui_command_queue_capacity = 512;
    std::deque<RuntimeEvent> runtime_event_queue;
    std::size_t runtime_event_queue_capacity = 1024;
};

struct TaskDecisionState {
    TaskCategoryConfig config{};
    TaskCategoryScheduleState schedule{};
    TaskCategoryInferenceState inference_state{};
    TaskCategoryInferenceResult last_result{};
    std::uint64_t context_signature = 0;
    std::uint64_t ocr_signature = 0;
    std::uint64_t scene_signature = 0;
    std::uint64_t asr_signature = 0;
    TaskPrimaryCategory primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory secondary = TaskSecondaryCategory::Unknown;
    RuntimeErrorInfo error_info{};
};

struct RuntimeWindowLayoutState {
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float size_w = 0.0f;
    float size_h = 0.0f;
    bool collapsed = false;
    bool initialized = false;
};

struct WorkspacePanelState {
    bool show_workspace_window = true;
    bool show_overview_window = true;
    bool show_editor_window = true;
    bool show_timeline_window = true;
    bool show_perception_window = true;
    bool show_ocr_window = true;
    bool show_mapping_window = true;
    bool show_asr_chat_window = true;
    bool show_plugin_worker_window = true;
    bool show_chat_window = true;
    bool show_error_window = true;
    bool show_inspector_window = true;
    bool show_reminder_window = true;
    bool show_plugin_quick_control_window = true;
    bool show_plugin_detail_window = false;
    std::string manual_docking_ini;
    WorkspaceLayoutMode layout_mode = WorkspaceLayoutMode::Preset;
    WorkspaceMode last_applied_mode = WorkspaceMode::Debug;
    bool preset_apply_requested = false;
    bool manual_layout_reset_requested = false;
    bool manual_layout_pending_load = false;
    bool manual_layout_save_suppressed = false;
    bool manual_layout_save_pending = false;
    float manual_layout_save_debounce_remaining_sec = 0.0f;
};

struct WorkspaceRuntimeState {
    RuntimeWindowLayoutState runtime_debug_window_layout{};
    RuntimeWindowLayoutState inspector_window_layout{};
    RuntimeWindowLayoutState reminder_window_layout{};
    WorkspacePanelState panels{};
    WorkspaceMode mode = WorkspaceMode::Debug;
    bool dock_rebuild_requested = false;
};

struct PluginRuntimeState {
    std::unique_ptr<IInferenceAdapter> inference_adapter;
    bool ready = false;
    PluginParamBlendMode param_blend_mode = PluginParamBlendMode::Override;
    BehaviorFusionConfig behavior_fusion_config{};

    std::vector<PluginConfigEntry> config_entries;
    std::unordered_map<std::string, PluginConfigCacheEntry> config_entry_cache;
    std::unordered_map<std::string, bool> enabled_states;
    int selected_entry_index = -1;
    bool config_refresh_requested = false;
    std::string config_scan_error;
    char name_input[128] = "";
    char edit_model_id_input[128] = "";
    char edit_onnx_input[260] = "";
    char edit_extra_onnx_input[512] = "";
    std::string edit_config_path;
    std::string switch_status;
    std::string switch_error;
    std::string delete_status;
    std::string delete_error;

    bool detail_edit_loaded = false;
    std::string detail_edit_source;
    char detail_edit_model_id_input[128] = "";
    char detail_edit_onnx_input[260] = "";
    char detail_edit_extra_onnx_input[512] = "";
    char detail_edit_labels_input[260] = "";
    char detail_edit_det_input[260] = "";
    char detail_edit_rec_input[260] = "";
    char detail_edit_keys_input[260] = "";
    char detail_edit_model_input[260] = "";

    std::vector<UnifiedPluginEntry> unified_entries;
    int unified_selected_index = -1;
    bool unified_refresh_requested = false;
    std::string unified_scan_error;
    std::vector<DefaultPluginCatalogEntry> default_plugin_catalog_entries;
    bool default_plugin_catalog_refresh_requested = false;
    std::string default_plugin_catalog_error;
    char unified_name_input[128] = "";
    char unified_new_name_input[128] = "";
    bool show_unified_create_modal = false;
    std::string unified_switch_status;
    std::string unified_switch_error;
    std::string unified_create_status;
    std::string unified_create_error;
    std::string unified_delete_status;
    std::string unified_delete_error;

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

    std::unordered_map<std::string, std::deque<PluginLogEntry>> logs;

    PluginDetailKind detail_kind = PluginDetailKind::None;
    std::string detail_title;
    std::string detail_source;
    std::vector<std::string> detail_assets;
    std::string detail_backend;
    std::string detail_status;
    std::string detail_last_error;

    std::uint64_t total_update_count = 0;
    std::uint64_t timeout_count = 0;
    std::uint64_t exception_count = 0;
    std::uint64_t internal_error_count = 0;
    std::uint64_t disable_count = 0;
    std::uint64_t recover_count = 0;
    double timeout_rate = 0.0;
    double last_latency_ms = 0.0;
    double avg_latency_ms = 0.0;
    double latency_p50_ms = 0.0;
    double latency_p95_ms = 0.0;
    double success_rate = 0.0;
    float stats_poll_accum_sec = 0.25f;
    float stats_poll_interval_sec = 0.25f;
    int current_update_hz = 60;
    bool auto_disabled = false;
    std::string last_error;
    std::string route_selected = "unknown";
    double route_scene_score = 0.0;
    double route_task_score = 0.0;
    double route_presence_score = 0.0;
    double route_total_score = 0.0;
    std::vector<std::string> route_rejected_summary;
    std::uint64_t panel_state_version = 0;

    RuntimeErrorInfo error_info{};
};

struct RuntimeObservabilityState {
    bool log_enabled = true;
    float log_interval_sec = 3.0f;
    float log_accum_sec = 0.0f;

    std::vector<double> scene_latency_window_ms;
    std::vector<double> ocr_latency_window_ms;
    std::vector<double> face_latency_window_ms;
    std::vector<double> latency_p95_scratch_ms;
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
};

struct AsrAsyncRequest {
    std::uint64_t seq = 0;
    std::uint64_t provider_generation = 0;
    AsrAudioChunk chunk;
    AsrRecognitionOptions options;
};

struct AsrAsyncResultPacket {
    std::uint64_t seq = 0;
    std::uint64_t provider_generation = 0;
    bool ok = false;
    AsrRecognitionResult result;
    std::string error;
    double audio_sec = 0.0;
};

struct AsrAsyncRuntimeState {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<AsrAsyncRequest> request_queue;
    std::deque<AsrAsyncResultPacket> completed_queue;
    bool stop_requested = false;
    bool worker_busy = false;
    std::uint64_t next_request_seq = 0;
    std::size_t max_request_queue_size = 4;
    std::size_t max_completed_queue_size = 8;
    std::int64_t dropped_request_count = 0;
};

struct RuntimeCoreState {
    RuntimeWindowState window_state{};
    RuntimeAudioState audio_state{};

    ModelRuntime model;
    bool model_loaded = false;
    float model_time = 0.0f;
};

struct RuntimeUiState {
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
};

struct RuntimeEditorState {
    bool edit_mode = false;
    int selected_part_index = -1;
    bool dragging_part = false;
    bool dragging_pivot = false;
    float drag_last_x = 0.0f;
    float drag_last_y = 0.0f;
    float drag_start_mouse_x = 0.0f;
    float drag_start_mouse_y = 0.0f;
    float drag_start_world_x = 0.0f;
    float drag_start_world_y = 0.0f;
    float drag_start_pos_x = 0.0f;
    float drag_start_pos_y = 0.0f;
    float drag_start_pivot_x = 0.0f;
    float drag_start_pivot_y = 0.0f;

    // 缂傚倹鐗炵欢顐﹀闯閵娧勬毎閻㈩垰鍟抽～瀣炊閹惧懐绀刾an/zoom闁挎稑顧€缁辨繃绋夊鍥ㄧ函闁规亽鍎伴幈銊╁绩鐟欏媭渚€宕圭€ｎ亜缍侀柟骞垮灪閺嗙喖骞戦琛″亾?
    float editor_view_pan_x = 0.0f;
    float editor_view_pan_y = 0.0f;
    float editor_view_zoom = 1.0f;
    bool editor_view_dragging = false;
    float editor_view_drag_last_x = 0.0f;
    float editor_view_drag_last_y = 0.0f;

    AxisConstraint axis_constraint = AxisConstraint::None;
    bool snap_enabled = false;
    float snap_grid = 10.0f;
    float editor_drag_sensitivity = 1.0f;
    float editor_gizmo_sensitivity = 1.0f;
    bool editor_snap_active_x = false;
    bool editor_snap_active_y = false;
    float editor_snap_world_x = 0.0f;
    float editor_snap_world_y = 0.0f;

    bool dragging_model_whole = false;
    float dragging_model_last_x = 0.0f;
    float dragging_model_last_y = 0.0f;

    bool property_panel_enabled = true;
    int selected_editor_prop = 0;

    // 閻犙冨缁噣寮?+ Inspector 闁艰鲸鏌ㄦ慨?
    char resource_tree_filter[128] = "";
    bool resource_tree_auto_expand_matches = true;
    int selected_deformer_type = 0; // 0=Warp, 1=Rotation

    // 闁归攱鍎宠ぐ鍥╃驳閺嶎偅娈?
    bool pick_lock_filter_enabled = true;
    bool pick_scope_filter_enabled = false;
    int pick_scope_mode = 0; // 0=All,1=Selected,2=Children
    bool pick_name_filter_enabled = false;
    char pick_name_filter[128] = "";
    bool pick_cycle_enabled = false;
    int pick_cycle_offset = 0;

    WorkspaceRuntimeState workspace_ui{};

    // 闁告瑥鍊归弳鐔兼閵忊剝绶插褏鍋涘閬嶆晬濮橆剙鐎荤紓浣稿閳ь兛鐒﹂幃宕囨椤厾鐟㈤柟闈涚秺閸ｈ櫣绱掗幋婵堟毎闁挎稑婀禝 闁绘鍩栭埀顑跨筏缁?
    int param_group_mode = 0; // 0=prefix, 1=semantic
    int selected_param_group_index = 0;
    char param_search[128] = "";
    int batch_bind_prop_type = 0; // 閻庣數鎳撶花?BindingType 闁哄鐭俊鍥磹?
    int batch_bind_template_index = 0; // 0=Custom, >=1 閻炴稏鍔庨妵姘熼埄鍐╃凡閹兼潙绻愯ぐ?
    float batch_bind_in_min = -1.0f;
    float batch_bind_in_max = 1.0f;
    float batch_bind_out_min = -1.0f;
    float batch_bind_out_max = 1.0f;
    bool editor_param_panel_expanded = true;
    bool editor_param_quick_expanded = true;
    bool editor_param_group_table_expanded = true;
    bool editor_param_batch_bind_expanded = true;

    // 闁哄啫鐖煎Λ鎸庢姜?v1
    bool timeline_enabled = false;
    int timeline_selected_channel_index = 0;
    float timeline_cursor_sec = 0.0f;
    float timeline_duration_sec = 3.0f;
    bool timeline_snap_enabled = true;
    int timeline_snap_mode = 0; // 0=闁轰礁鐡ㄩ弳鐔烘暜? 1=0.1s, 2=闁圭虎鍘介弬浣瑰緞?
    float timeline_snap_fps = 30.0f;
    std::vector<int> timeline_selected_keyframe_indices;
    std::vector<std::uint64_t> timeline_selected_keyframe_ids;
    std::vector<TimelineKeyframe> timeline_keyframe_clipboard;
    std::vector<EditCommand> undo_stack;
    std::vector<EditCommand> redo_stack;
    EditorControllerState editor_controller_state{};

    GizmoHandle gizmo_hover_handle = GizmoHandle::None;
    GizmoHandle gizmo_active_handle = GizmoHandle::None;
    bool gizmo_dragging = false;
    float gizmo_drag_start_mouse_x = 0.0f;
    float gizmo_drag_start_mouse_y = 0.0f;
    float gizmo_drag_start_pos_x = 0.0f;
    float gizmo_drag_start_pos_y = 0.0f;
    float gizmo_drag_start_world_x = 0.0f;
    float gizmo_drag_start_world_y = 0.0f;
    float gizmo_drag_start_rot_deg = 0.0f;
    float gizmo_drag_start_scale_x = 1.0f;
    float gizmo_drag_start_scale_y = 1.0f;
    float gizmo_drag_start_angle = 0.0f;
    float gizmo_drag_start_dist = 1.0f;

    bool edit_capture_active = false;
    EditCommand active_edit_cmd;

    std::string editor_status;
    float editor_status_ttl = 0.0f;

    bool editor_autosave_enabled = true;
    float editor_autosave_interval_sec = 120.0f;
    float editor_autosave_accum_sec = 0.0f;
    bool editor_autosave_recovery_available = false;
    bool editor_autosave_recovery_prompted = false;
    bool editor_autosave_recovery_checked = false;
    std::string editor_autosave_path;
    std::string editor_autosave_last_error;

    // 鐎规悶鍎抽埢鑲╃棯瑜岀槐鎵嫚濠靛洦鐎ù鐘侯啇缁辨獤roject.json闁挎稑顦抽惌鎯ь嚗?
    std::string current_project_path = "assets/project.json";
    bool editor_project_dirty = false;

    int editor_history_selected_index = -1;

    float debug_fps = 0.0f;
    float debug_frame_ms = 0.0f;
    float debug_fps_accum_sec = 0.0f;
    int debug_fps_accum_frames = 0;

    bool gui_enabled = true;

    InteractionControllerState interaction_state{};
};

struct RuntimeServiceState {
    ReminderService reminder_service;
    bool reminder_ready = false;
    float reminder_poll_accum_sec = 0.0f;
    char reminder_title_input[128] = "";
    int reminder_after_min = 10;
    char reminder_search[128] = "";
    int reminder_filter_mode = 0; // 0=all,1=upcoming,2=overdue
    int reminder_sort_mode = 0;   // 0=due asc,1=due desc,2=overdue first
    int reminder_page = 0;
    std::vector<ReminderItem> reminder_upcoming;
    std::string reminder_last_error;
};

struct RuntimePerceptionHostState {
    PerceptionPipeline perception_pipeline;
    PerceptionPipelineState perception_state;

    RuntimeFeatureFlags feature_flags{};
    float face_map_min_confidence = 0.45f;
    float face_map_head_pose_deadzone_deg = 2.0f;
    float face_map_yaw_max_deg = 25.0f;
    float face_map_pitch_max_deg = 18.0f;
    float face_map_eye_open_threshold = 0.25f;
    float face_map_param_weight = 0.65f;
    float face_map_smooth_alpha = 0.35f;

    // 濞磋偐濮甸崝鍛村闯閵娿儳纾介悽顖氭啞濡炲倿鎯?fallback 濠殿喗瀵ч埀顑跨劍鑶╅柡澶婂皡缁辨瑨銇愰幒宥囶伇闁告牗鐗曞顒勫极閹殿喒鏁勯梻鍌濇彧缁?
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
};

struct RuntimeAsrChatState {
    std::unique_ptr<IAsrProvider> asr_provider;
    std::mutex asr_provider_mutex;
    std::uint64_t asr_provider_generation = 0;
    bool asr_ready = false;
    float asr_poll_accum_sec = 0.0f;
    std::string asr_last_error;
    AsrRecognitionResult asr_last_result;
    // ASR 濞村吋淇洪惁钘壝规担铏瑰閻庢稒锕槐娆撴儗椤撶喐鍩傚☉鎾筹梗缁楀懘寮崶椋庣
    AsrSessionState asr_session_state{};

    std::unique_ptr<IChatProvider> chat_provider;
    bool chat_ready = false;
    bool prefer_cloud_chat = true;
    std::string chat_last_error;
    std::string chat_last_switch_reason;
    char chat_input[512] = "Hello, introduce yourself";
    std::string chat_last_answer;

    EnergyVadSegmenter asr_vad{VadConfig{}};
    std::vector<float> asr_audio_buffer;
    std::size_t asr_audio_buffer_capacity = 320;
    int asr_frame_samples = 320; // 20ms @ 16k
    AsrAsyncRuntimeState asr_async_state{};

    // ASR 闁烩晜鍨剁敮鍫曞箰閸ャ劎鍨?
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
    std::uint64_t panel_state_version = 0;
};

struct RuntimePluginHostState {
    PluginRuntimeState plugin{};
};

struct RuntimeObservabilityStateHost {
    RuntimeObservabilityState observability{};

    RuntimeErrorInfo asr_error_info{};
    RuntimeErrorInfo chat_error_info{};
    RuntimeErrorInfo reminder_error_info{};

    TaskDecisionState task_decision{};
};

struct AppRuntime : RuntimeCoreState,
                    RuntimeUiState,
                    RuntimeEditorState,
                    RuntimeServiceState,
                    RuntimePerceptionHostState,
                    RuntimeAsrChatState,
                    RuntimePluginHostState,
                    RuntimeObservabilityStateHost {};

}  // namespace desktoper2D
