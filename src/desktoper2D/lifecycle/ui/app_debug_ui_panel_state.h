#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

struct TimelineOptionItem {
    std::string label;
    int value = 0;
};

struct TimelineChannelViewState {
    std::string id;
    int param_index = 0;
    bool enabled = false;
    TimelineInterpolation interpolation = TimelineInterpolation::Linear;
    TimelineWrapMode wrap = TimelineWrapMode::Clamp;
    std::vector<TimelineKeyframe> keyframes;
};

struct TimelinePanelReadonlyState {
    bool has_model_params = false;
    bool has_channels = false;
    bool can_add_channel = false;
    bool can_delete_channel = false;
    bool can_add_or_update_keyframe = false;
    bool can_copy_selected_keyframes = false;
    bool can_paste = false;
    bool can_remove_last_keyframe = false;
    bool can_undo = false;
    bool can_redo = false;
    int selected_param_index = 0;
    int selected_channel_index = 0;
    int selected_keyframe_count = 0;
    int clipboard_keyframe_count = 0;
    int undo_count = 0;
    int redo_count = 0;
    float cursor_sec = 0.0f;
    float duration_sec = 0.0f;
    float snap_fps = 30.0f;
    float active_param_min_value = -1.0f;
    float active_param_max_value = 1.0f;
    bool snap_enabled = false;
    int snap_mode = 0;
    bool drag_snapshot_captured = false;
    int dragging_keyframe_index = -1;
    int dragging_channel_index = -1;
    bool box_select_active = false;
    float box_select_start_x = 0.0f;
    float box_select_start_y = 0.0f;
    float box_select_end_x = 0.0f;
    float box_select_end_y = 0.0f;
    std::vector<int> selected_keyframe_indices;
    std::vector<std::uint64_t> selected_keyframe_ids;
    std::vector<TimelineOptionItem> channel_options;
    std::vector<TimelineOptionItem> param_options;
    TimelineChannelViewState active_channel;
};

struct TimelinePanelEditableState {
    bool timeline_enabled = false;
    float cursor_sec = 0.0f;
    float duration_sec = 3.0f;
    bool snap_enabled = true;
    int snap_mode = 0;
    float snap_fps = 30.0f;
    int selected_channel_index = 0;
};

struct TimelinePanelState {
    TimelinePanelReadonlyState view;
    TimelinePanelEditableState form;
};

struct TimelineInteractionSnapshot {
    bool drag_snapshot_captured = false;
    int dragging_keyframe_index = -1;
    int dragging_channel_index = -1;
    bool box_select_active = false;
    float box_select_start_x = 0.0f;
    float box_select_start_y = 0.0f;
    float box_select_end_x = 0.0f;
    float box_select_end_y = 0.0f;
};

struct EditorParamGroupOption {
    std::string label;
    std::string preview;
    std::vector<int> param_indices;
};

struct EditorParamRowState {
    int param_index = -1;
    std::string param_id;
    float min_value = 0.0f;
    float max_value = 0.0f;
    float default_value = 0.0f;
    float current_value = 0.0f;
    float target_value = 0.0f;
    bool selected = false;
};

struct EditorBatchBindState {
    int bind_prop_type = 0;
    float bind_in_min = -1.0f;
    float bind_in_max = 1.0f;
    float bind_out_min = -1.0f;
    float bind_out_max = 1.0f;
    bool can_apply_to_selected_part = false;
    bool can_apply_to_all_parts = false;
    std::string selected_part_label;
};

struct EditorPanelReadonlyState {
    bool model_loaded = false;
    bool has_model_params = false;
    bool has_param_groups = false;
    bool show_debug_stats = false;
    bool manual_param_mode = false;
    bool hair_spring_enabled = false;
    bool simple_mask_enabled = false;
    bool head_pat_hovering = false;
    float head_pat_react_ttl = 0.0f;
    float head_pat_progress = 0.0f;
    bool feature_scene_classifier_enabled = false;
    bool feature_ocr_enabled = false;
    bool feature_face_emotion_enabled = false;
    bool feature_asr_enabled = false;
    bool feature_plugin_enabled = true;
    int param_group_mode = 0;
    std::string param_search;
    int selected_param_group_index = 0;
    int selected_group_param_count = 0;
    std::string selected_group_label;
    std::string selected_group_preview;
    std::vector<EditorParamGroupOption> group_options;
    std::vector<int> selected_group_param_indices;
    std::vector<EditorParamRowState> selected_group_param_rows;
    EditorBatchBindState batch_bind;
};

struct EditorPanelState {
    EditorPanelReadonlyState view;
};

enum class EditorPanelActionType {
    SetShowDebugStats,
    SetManualParamMode,
    SetHairSpringEnabled,
    SetSimpleMaskEnabled,
    SetFeatureSceneClassifierEnabled,
    SetFeatureOcrEnabled,
    SetFeatureFaceEmotionEnabled,
    SetFeatureAsrEnabled,
    SetFeaturePluginEnabled,
    SetParamGroupMode,
    SetParamSearch,
    SelectParamGroup,
    SelectParam,
    SetParamTargetValue,
    SetBatchBindPropType,
    SetBatchBindInMin,
    SetBatchBindInMax,
    SetBatchBindOutMin,
    SetBatchBindOutMax,
    ApplyBatchBindToSelectedPart,
    ApplyBatchBindToAllParts,
};

struct EditorPanelAction {
    EditorPanelActionType type = EditorPanelActionType::SetShowDebugStats;
    int int_value = 0;
    bool bool_value = false;
    float float_value = 0.0f;
    std::string text_value;
};

struct PerceptionPanelState {
    bool capture_ready = false;
    bool scene_ready = false;
    bool ocr_ready = false;
    bool facemesh_ready = false;
    bool feature_ocr_enabled = false;
    bool feature_face_emotion_enabled = false;
    bool face_detected = false;
    bool has_face_keypoints = false;
    bool has_ocr_lines = false;
    bool has_confidence_samples = false;
    int ocr_timeout_ms = 0;
    int ocr_det_input_size = 0;
    int ocr_det_effective_input_size = 0;
    int face_keypoint_count = 0;
    float capture_poll_interval_sec = 0.0f;
    double capture_success_rate = 0.0;
    double scene_avg_latency_ms = 0.0;
    double face_avg_latency_ms = 0.0;
    float ocr_avg_latency_ms = 0.0f;
    float ocr_discard_rate = 0.0f;
    std::int64_t capture_success_count = 0;
    std::int64_t capture_fail_count = 0;
    std::int64_t scene_total_runs = 0;
    std::int64_t face_total_runs = 0;
    std::int64_t ocr_total_kept_lines = 0;
    std::int64_t ocr_total_raw_lines = 0;
    std::int64_t ocr_total_dropped_low_conf_lines = 0;
    float ocr_low_conf_threshold = 0.0f;
    float ocr_conf_low_pct = 0.0f;
    float ocr_conf_mid_pct = 0.0f;
    float ocr_conf_high_pct = 0.0f;
    std::string scene_label;
    double scene_score = 0.0;
    std::string ocr_summary;
    std::string top_ocr_text;
    float top_ocr_score = 0.0f;
    std::string emotion_label;
    float emotion_score = 0.0f;
    float head_yaw_deg = 0.0f;
    float head_pitch_deg = 0.0f;
    float head_roll_deg = 0.0f;
    float eye_open_left = 0.0f;
    float eye_open_right = 0.0f;
    float eye_open_avg = 0.0f;
    std::string first_keypoint_name;
    float first_keypoint_x = 0.0f;
    float first_keypoint_y = 0.0f;
    float first_keypoint_score = 0.0f;
    std::string process_name;
    std::string window_title;
    std::string url_hint;
    std::string capture_error;
    std::string scene_error;
    std::string camera_error;
    std::string system_context_error;
    std::string ocr_error;
    std::string recent_error;
};

struct AsrChatPanelState {
    bool asr_ready = false;
    bool feature_asr_enabled = false;
    bool chat_ready = false;
    bool feature_chat_enabled = false;
    bool prefer_cloud_chat = true;
    bool observability_log_enabled = false;
    bool has_plugin_last_error = false;
    bool has_route_rejected_summary = false;
    bool can_send_chat = false;
    float observability_log_interval_sec = 0.0f;
    std::string asr_text;
    std::string asr_switch_reason;
    std::string asr_last_error;
    std::string chat_input;
    std::string chat_answer;
    std::string chat_switch_reason;
    std::string chat_last_error;
    std::string recent_error;
    double asr_rtf = 0.0;
    double asr_wer_proxy = 0.0;
    double asr_timeout_rate = 0.0;
    double asr_cloud_call_ratio = 0.0;
    double asr_cloud_success_ratio = 0.0;
    int plugin_update_hz = 0;
    std::uint64_t plugin_total_updates = 0;
    std::uint64_t plugin_timeout_count = 0;
    std::uint64_t plugin_exception_count = 0;
    std::uint64_t plugin_internal_error_count = 0;
    std::uint64_t plugin_disable_count = 0;
    std::uint64_t plugin_recover_count = 0;
    double plugin_timeout_rate = 0.0;
    bool plugin_auto_disabled = false;
    std::string plugin_last_error;
    std::string plugin_route_selected;
    double plugin_route_scene_score = 0.0;
    double plugin_route_task_score = 0.0;
    double plugin_route_presence_score = 0.0;
    double plugin_route_total_score = 0.0;
    std::vector<std::string> plugin_route_rejected_summary;
};

enum class TimelinePanelActionType {
    SetEnabled,
    SetCursorSec,
    SetDurationSec,
    SetSnapEnabled,
    SetSnapMode,
    SetSnapFps,
    SelectChannel,
    AddChannel,
    DeleteChannel,
    SetChannelEnabled,
    SetChannelInterpolation,
    SetChannelWrap,
    SetChannelTargetParam,
    AddOrUpdateKeyframeAtCursor,
    CopySelectedKeyframes,
    PasteAtCursor,
    RemoveLastKeyframe,
    Undo,
    Redo,
    BeginTrackDrag,
    UpdateTrackDrag,
    EndTrackDrag,
    BeginBoxSelect,
    UpdateBoxSelect,
    EndBoxSelect,
    SelectKeyframe,
    SetKeyframeInTangent,
    SetKeyframeOutTangent,
    SetKeyframeInWeight,
    SetKeyframeOutWeight,
};

struct TimelinePanelAction {
    TimelinePanelActionType type = TimelinePanelActionType::SetEnabled;
    int int_value = 0;
    int int_value2 = 0;
    float float_value = 0.0f;
    float float_value2 = 0.0f;
    bool bool_value = false;
    bool additive = false;
    std::uint64_t stable_id = 0;
};

enum class PerceptionPanelActionType {
    SetOcrTimeoutMs,
    SetOcrDetInputSize,
    SetCapturePollIntervalSec,
};

struct PerceptionPanelAction {
    PerceptionPanelActionType type = PerceptionPanelActionType::SetOcrTimeoutMs;
    int int_value = 0;
    float float_value = 0.0f;
};

enum class AsrChatPanelActionType {
    SetObservabilityLogEnabled,
    SetObservabilityLogIntervalSec,
    SetChatEnabled,
    SetPreferCloudChat,
    SetChatInput,
    SendChat,
};

struct AsrChatPanelAction {
    AsrChatPanelActionType type = AsrChatPanelActionType::SetObservabilityLogEnabled;
    bool bool_value = false;
    float float_value = 0.0f;
    std::string text_value;
};

TimelinePanelState BuildTimelinePanelState(const AppRuntime &runtime);
EditorPanelState BuildEditorPanelState(const AppRuntime &runtime);
PerceptionPanelState BuildPerceptionPanelState(const AppRuntime &runtime);
AsrChatPanelState BuildAsrChatPanelState(const AppRuntime &runtime);

TimelineInteractionSnapshot GetTimelineInteractionSnapshot();
void RestoreTimelineSelectionFromHistory(AppRuntime &runtime, const std::vector<std::uint64_t> &selected_ids);
void ClearTimelineInteractionState();
void ApplyTimelinePanelAction(AppRuntime &runtime, const TimelinePanelAction &action);
void ApplyEditorPanelAction(AppRuntime &runtime, const EditorPanelAction &action);

void ApplyTimelinePanelActionImpl(AppRuntime &runtime, const TimelinePanelAction &action);
void ApplyEditorPanelActionImpl(AppRuntime &runtime, const EditorPanelAction &action);
void ApplyPerceptionPanelAction(AppRuntime &runtime, const PerceptionPanelAction &action);
void ApplyAsrChatPanelAction(AppRuntime &runtime, const AsrChatPanelAction &action);

}  // namespace desktoper2D
