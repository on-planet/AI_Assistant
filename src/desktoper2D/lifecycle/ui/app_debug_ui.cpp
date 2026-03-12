#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_iostream.h>

#include "app_debug_ui_internal.h"
#include "app_debug_ui_panel_state.h"
#include "app_debug_ui_presenter.h"
#include "app_debug_ui_widgets.h"
#include "imgui.h"

#include "desktoper2D/core/json.h"
#include "desktoper2D/lifecycle/editor/editor_session_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/ui/ui_empty_state.h"

namespace desktoper2D {

namespace {


void ResetPerceptionRuntimeState(PerceptionPipelineState &state) {
    state.screen_capture_poll_accum_sec = 0.0f;
    state.screen_capture_last_error.clear();
    state.screen_capture_success_count = 0;
    state.screen_capture_fail_count = 0;
    ClearRuntimeError(state.capture_error_info);

    state.scene_classifier_last_error.clear();
    state.scene_result = SceneClassificationResult{};
    state.scene_total_runs = 0;
    state.scene_total_latency_ms = 0;
    state.scene_avg_latency_ms = 0.0f;
    ClearRuntimeError(state.scene_error_info);

    state.ocr_last_error.clear();
    state.ocr_result = OcrResult{};
    state.ocr_last_stable_result = OcrResult{};
    state.ocr_skipped_due_timeout = false;
    state.ocr_total_runs = 0;
    state.ocr_total_latency_ms = 0;
    state.ocr_avg_latency_ms = 0.0f;
    state.ocr_preprocess_det_avg_ms = 0.0f;
    state.ocr_infer_det_avg_ms = 0.0f;
    state.ocr_preprocess_rec_avg_ms = 0.0f;
    state.ocr_infer_rec_avg_ms = 0.0f;
    state.ocr_total_raw_lines = 0;
    state.ocr_total_kept_lines = 0;
    state.ocr_total_dropped_low_conf_lines = 0;
    state.ocr_discard_rate = 0.0f;
    state.ocr_conf_low_count = 0;
    state.ocr_conf_mid_count = 0;
    state.ocr_conf_high_count = 0;
    state.ocr_summary_candidate.clear();
    state.ocr_summary_stable.clear();
    state.ocr_summary_consistent_count = 0;
    ClearRuntimeError(state.ocr_error_info);

    state.system_context_snapshot = SystemContextSnapshot{};
    state.system_context_last_error.clear();
    ClearRuntimeError(state.system_context_error_info);

    state.camera_facemesh_last_error.clear();
    state.face_emotion_result = FaceEmotionResult{};
    state.face_total_runs = 0;
    state.face_total_latency_ms = 0;
    state.face_avg_latency_ms = 0.0f;
    ClearRuntimeError(state.facemesh_error_info);

    state.blackboard = PerceptionBlackboard{};
}

void ResetAllRuntimeErrorCounters(AppRuntime &runtime) {
    auto reset_err = [](RuntimeErrorInfo &err) {
        ClearRuntimeError(err);
        err.detail.clear();
        err.count = 0;
        err.degraded_count = 0;
    };

    reset_err(runtime.perception_state.capture_error_info);
    reset_err(runtime.perception_state.scene_error_info);
    reset_err(runtime.perception_state.ocr_error_info);
    reset_err(runtime.perception_state.system_context_error_info);
    reset_err(runtime.perception_state.facemesh_error_info);
    reset_err(runtime.plugin_error_info);
    reset_err(runtime.asr_error_info);
    reset_err(runtime.chat_error_info);
    reset_err(runtime.reminder_error_info);
}


bool ExportRuntimeSnapshotJson(const AppRuntime &runtime, const char *path, std::string *out_error) {
    const JsonValue snapshot = BuildRuntimeSnapshotJson(runtime);
    const std::string text = StringifyJson(snapshot, 2);

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        if (out_error) {
            *out_error = std::string("open snapshot file failed: ") + SDL_GetError();
        }
        return false;
    }

    const size_t n = text.size();
    const size_t w = SDL_WriteIO(io, text.data(), n);
    SDL_CloseIO(io);

    if (w != n) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (out_error) {
            *out_error = "write snapshot file failed";
        }
        return false;
    }

    if (out_error) {
        out_error->clear();
    }
    return true;
}

void TriggerSingleStepSampling(AppRuntime &runtime) {
    runtime.perception_state.screen_capture_poll_accum_sec =
        std::max(0.1f, runtime.perception_state.screen_capture_poll_interval_sec);
    runtime.reminder_poll_accum_sec = 1.0f;
    runtime.asr_poll_accum_sec = 0.02f;
    runtime.runtime_observability_log_accum_sec =
        std::max(0.2f, runtime.runtime_observability_log_interval_sec);
}

std::string DetectParamPrefix(const std::string &param_id) {
    static std::size_t empty_id_count = 0;
    static std::size_t abnormal_id_count = 0;

    if (param_id.empty()) {
        ++empty_id_count;
        SDL_Log("[ParamGroup] empty param id encountered (count=%zu)", empty_id_count);
        return "misc";
    }
    const std::size_t us = param_id.find('_');
    if (us != std::string::npos && us > 0) {
        return param_id.substr(0, us);
    }
    std::size_t cut = 0;
    while (cut < param_id.size()) {
        const unsigned char ch = static_cast<unsigned char>(param_id[cut]);
        if ((ch >= 'A' && ch <= 'Z' && cut > 0) || (ch >= '0' && ch <= '9')) {
            break;
        }
        ++cut;
    }
    if (cut == 0) {
        ++abnormal_id_count;
        SDL_Log("[ParamGroup] abnormal param id: '%s' (count=%zu)", param_id.c_str(), abnormal_id_count);
        return "misc";
    }
    return param_id.substr(0, cut);
}

std::string DetectParamSemanticGroup(const std::string &param_id) {
    std::string id_lower = param_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (id_lower.find("eye") != std::string::npos || id_lower.find("blink") != std::string::npos) return "眼睛";
    if (id_lower.find("head") != std::string::npos || id_lower.find("neck") != std::string::npos) return "头部";
    if (id_lower.find("brow") != std::string::npos || id_lower.find("mouth") != std::string::npos ||
        id_lower.find("lip") != std::string::npos || id_lower.find("cheek") != std::string::npos ||
        id_lower.find("nose") != std::string::npos || id_lower.find("emotion") != std::string::npos ||
        id_lower.find("smile") != std::string::npos || id_lower.find("angry") != std::string::npos) {
        return "表情";
    }
    if (id_lower.find("window") != std::string::npos || id_lower.find("opacity") != std::string::npos ||
        id_lower.find("clickthrough") != std::string::npos || id_lower.find("click_through") != std::string::npos) {
        return "窗口";
    }
    if (id_lower.find("behavior") != std::string::npos || id_lower.find("idle") != std::string::npos ||
        id_lower.find("debug") != std::string::npos || id_lower.find("manual") != std::string::npos) {
        return "行为";
    }
    if (id_lower.find("hair") != std::string::npos || id_lower.find("bang") != std::string::npos) return "头发";
    if (id_lower.find("body") != std::string::npos || id_lower.find("arm") != std::string::npos) return "身体";
    return "其他";
}

bool ParamMatchesSearch(const std::string &param_id, const char *search_text) {
    if (search_text == nullptr || search_text[0] == '\0') {
        return true;
    }
    std::string id_lower = param_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string needle = search_text;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return id_lower.find(needle) != std::string::npos;
}

using ParamGroup = std::pair<std::string, std::vector<int>>;

std::vector<ParamGroup> BuildParamGroups(const AppRuntime &runtime, int group_mode, const char *search_text) {
    std::map<std::string, std::vector<int>, std::less<>> grouped;
    for (int i = 0; i < static_cast<int>(runtime.model.parameters.size()); ++i) {
        const auto &p = runtime.model.parameters[static_cast<std::size_t>(i)];
        if (!ParamMatchesSearch(p.id, search_text)) {
            continue;
        }
        const std::string key = (group_mode == 1) ? DetectParamSemanticGroup(p.id) : DetectParamPrefix(p.id);
        grouped[key].push_back(i);
    }

    std::vector<ParamGroup> out;
    out.reserve(grouped.size());
    for (auto &kv : grouped) {
        out.emplace_back(kv.first, std::move(kv.second));
    }
    return out;
}

const char *BindingTypeNameUi(BindingType t) {
    switch (t) {
        case BindingType::PosX: return "PosX";
        case BindingType::PosY: return "PosY";
        case BindingType::RotDeg: return "RotDeg";
        case BindingType::ScaleX: return "ScaleX";
        case BindingType::ScaleY: return "ScaleY";
        case BindingType::Opacity: return "Opacity";
        default: return "Unknown";
    }
}

void UpsertBinding(ModelPart &part, int param_index, BindingType type, float in_min, float in_max, float out_min, float out_max) {
    for (auto &b : part.bindings) {
        if (b.param_index == param_index && b.type == type) {
            b.in_min = in_min;
            b.in_max = in_max;
            b.out_min = out_min;
            b.out_max = out_max;
            return;
        }
    }

    ParamBinding b{};
    b.param_index = param_index;
    b.type = type;
    b.in_min = in_min;
    b.in_max = in_max;
    b.out_min = out_min;
    b.out_max = out_max;
    part.bindings.push_back(b);
}

std::uint64_t NextUiTimelineStableId() {
    static std::uint64_t next_id = 1ull << 62;
    return next_id++;
}

void EnsureTimelineKeyframeStableIds(AnimationChannel &ch) {
    for (auto &kf : ch.keyframes) {
        if (kf.stable_id == 0) {
            kf.stable_id = NextUiTimelineStableId();
        }
    }
}

std::vector<int> BuildTimelineSelectedIndices(const AnimationChannel &ch,
                                              const std::vector<std::uint64_t> &selected_ids) {
    std::vector<int> indices;
    indices.reserve(selected_ids.size());
    for (std::size_t i = 0; i < ch.keyframes.size(); ++i) {
        if (std::find(selected_ids.begin(), selected_ids.end(), ch.keyframes[i].stable_id) != selected_ids.end()) {
            indices.push_back(static_cast<int>(i));
        }
    }
    return indices;
}

void NormalizeTimelineSelection(AppRuntime &runtime, AnimationChannel &ch) {
    desktoper2D::EnsureTimelineKeyframeStableIds(ch);

    std::vector<std::uint64_t> normalized_ids;
    normalized_ids.reserve(runtime.timeline_selected_keyframe_ids.size() + runtime.timeline_selected_keyframe_indices.size());

    auto append_id = [&](std::uint64_t stable_id) {
        if (stable_id == 0) {
            return;
        }
        if (std::find(normalized_ids.begin(), normalized_ids.end(), stable_id) == normalized_ids.end()) {
            normalized_ids.push_back(stable_id);
        }
    };

    for (std::uint64_t stable_id : runtime.timeline_selected_keyframe_ids) {
        append_id(stable_id);
    }
    for (int idx : runtime.timeline_selected_keyframe_indices) {
        if (idx >= 0 && idx < static_cast<int>(ch.keyframes.size())) {
            append_id(ch.keyframes[static_cast<std::size_t>(idx)].stable_id);
        }
    }

    runtime.timeline_selected_keyframe_ids.clear();
    for (std::uint64_t stable_id : normalized_ids) {
        for (const auto &kf : ch.keyframes) {
            if (kf.stable_id == stable_id) {
                runtime.timeline_selected_keyframe_ids.push_back(stable_id);
                break;
            }
        }
    }
    runtime.timeline_selected_keyframe_indices = desktoper2D::BuildTimelineSelectedIndices(ch, runtime.timeline_selected_keyframe_ids);
}

bool IsTimelineKeyframeSelected(const AppRuntime &runtime, const TimelineKeyframe &kf) {
    return std::find(runtime.timeline_selected_keyframe_ids.begin(),
                     runtime.timeline_selected_keyframe_ids.end(),
                     kf.stable_id) != runtime.timeline_selected_keyframe_ids.end();
}

void SelectTimelineKeyframeById(AppRuntime &runtime, AnimationChannel &ch, std::uint64_t stable_id, bool additive) {
    desktoper2D::EnsureTimelineKeyframeStableIds(ch);
    if (!additive) {
        runtime.timeline_selected_keyframe_ids.clear();
    }
    if (stable_id != 0 && std::find(runtime.timeline_selected_keyframe_ids.begin(),
                                    runtime.timeline_selected_keyframe_ids.end(),
                                    stable_id) == runtime.timeline_selected_keyframe_ids.end()) {
        runtime.timeline_selected_keyframe_ids.push_back(stable_id);
    }
    desktoper2D::NormalizeTimelineSelection(runtime, ch);
}

void ClearTimelineSelection(AppRuntime &runtime, AnimationChannel &ch) {
    runtime.timeline_selected_keyframe_ids.clear();
    runtime.timeline_selected_keyframe_indices.clear();
    desktoper2D::NormalizeTimelineSelection(runtime, ch);
}

EditCommand MakeTimelineEditCommand(const AnimationChannel &channel,
                                    const std::vector<TimelineKeyframe> &before_keyframes,
                                    const std::vector<TimelineKeyframe> &after_keyframes,
                                    const std::vector<std::uint64_t> &before_selected_ids,
                                    const std::vector<std::uint64_t> &after_selected_ids) {
    EditCommand cmd{};
    cmd.type = EditCommand::Type::Timeline;
    cmd.channel_id = channel.id;
    cmd.before_keyframes = before_keyframes;
    cmd.after_keyframes = after_keyframes;
    cmd.before_selected_keyframe_ids = before_selected_ids;
    cmd.after_selected_keyframe_ids = after_selected_ids;
    return cmd;
}

void UpsertTimelineKeyframe(AnimationChannel &ch, float time_sec, float value) {
    desktoper2D::EnsureTimelineKeyframeStableIds(ch);
    for (auto &kf : ch.keyframes) {
        if (std::abs(kf.time_sec - time_sec) < 1e-4f) {
            kf.value = value;
            return;
        }
    }
    ch.keyframes.push_back(TimelineKeyframe{.stable_id = NextUiTimelineStableId(), .time_sec = time_sec, .value = value});
    std::sort(ch.keyframes.begin(), ch.keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
        return a.time_sec < b.time_sec;
    });
}

float EvalTimelinePreviewValue(const AnimationChannel &channel, float time_sec) {
    if (channel.keyframes.empty()) {
        return 0.0f;
    }
    if (channel.keyframes.size() == 1) {
        return channel.keyframes.front().value;
    }

    const auto &kfs = channel.keyframes;
    const float start_t = kfs.front().time_sec;
    const float end_t = kfs.back().time_sec;
    const float duration = std::max(1e-6f, end_t - start_t);

    float eval_t = time_sec;
    if (channel.timeline_wrap == TimelineWrapMode::Loop) {
        const float x = std::fmod(time_sec - start_t, duration);
        eval_t = start_t + (x < 0.0f ? (x + duration) : x);
    } else if (channel.timeline_wrap == TimelineWrapMode::PingPong) {
        const float period = duration * 2.0f;
        float x = std::fmod(time_sec - start_t, period);
        if (x < 0.0f) x += period;
        eval_t = (x <= duration) ? (start_t + x) : (end_t - (x - duration));
    }

    if (eval_t <= start_t) {
        return kfs.front().value;
    }
    if (eval_t >= end_t) {
        return kfs.back().value;
    }

    for (std::size_t i = 1; i < kfs.size(); ++i) {
        const TimelineKeyframe &a = kfs[i - 1];
        const TimelineKeyframe &b = kfs[i];
        if (eval_t <= b.time_sec) {
            if (channel.timeline_interp == TimelineInterpolation::Step) {
                return a.value;
            }
            const float span = std::max(1e-6f, b.time_sec - a.time_sec);
            const float t = std::clamp((eval_t - a.time_sec) / span, 0.0f, 1.0f);
            if (channel.timeline_interp == TimelineInterpolation::Linear) {
                return a.value + (b.value - a.value) * t;
            }

            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            const float w0 = std::clamp(a.out_weight, 0.0f, 1.0f);
            const float w1 = std::clamp(b.in_weight, 0.0f, 1.0f);
            const float m0 = a.out_tangent * span * w0;
            const float m1 = b.in_tangent * span * w1;
            return h00 * a.value + h10 * m0 + h01 * b.value + h11 * m1;
        }
    }

    return kfs.back().value;
}

float SnapTimelineTime(const AppRuntime &runtime, float t) {
    t = std::clamp(t, 0.0f, std::max(0.0f, runtime.timeline_duration_sec));
    if (!runtime.timeline_snap_enabled) {
        return t;
    }
    switch (runtime.timeline_snap_mode) {
        case 0: {
            const float fps = std::max(1.0f, runtime.timeline_snap_fps);
            return std::round(t * fps) / fps;
        }
        case 1:
            return std::round(t * 10.0f) / 10.0f;
        case 2:
            return runtime.timeline_cursor_sec;
        default:
            return t;
    }
}


bool TimelineKeyframeNearlyEqual(const TimelineKeyframe &a, const TimelineKeyframe &b) {
    return std::abs(a.time_sec - b.time_sec) < 1e-6f &&
           std::abs(a.value - b.value) < 1e-6f &&
           std::abs(a.in_tangent - b.in_tangent) < 1e-6f &&
           std::abs(a.out_tangent - b.out_tangent) < 1e-6f &&
           std::abs(a.in_weight - b.in_weight) < 1e-6f &&
           std::abs(a.out_weight - b.out_weight) < 1e-6f;
}

bool TimelineKeyframeListEqual(const std::vector<TimelineKeyframe> &lhs,
                               const std::vector<TimelineKeyframe> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!desktoper2D::TimelineKeyframeNearlyEqual(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

void PushTimelineEditCommand(AppRuntime &runtime,
                             const AnimationChannel &channel,
                             const std::vector<TimelineKeyframe> &before_keyframes,
                             const std::vector<TimelineKeyframe> &after_keyframes,
                             const std::vector<std::uint64_t> &before_selected_ids,
                             const std::vector<std::uint64_t> &after_selected_ids) {
    if (desktoper2D::TimelineKeyframeListEqual(before_keyframes, after_keyframes) && before_selected_ids == after_selected_ids) {
        return;
    }
    PushEditCommand(runtime.undo_stack,
                    runtime.redo_stack,
                    desktoper2D::MakeTimelineEditCommand(channel,
                                            before_keyframes,
                                            after_keyframes,
                                            before_selected_ids,
                                            after_selected_ids));
}

void RenderRuntimeEditorFeatureToggles(AppRuntime &runtime) {
    EditorPanelState panel_state = BuildEditorPanelState(runtime);

    ImGui::SeparatorText("Feature Toggles");
    bool feature_scene_classifier_enabled = panel_state.view.feature_scene_classifier_enabled;
    if (ImGui::Checkbox("Enable Scene Classifier", &feature_scene_classifier_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureSceneClassifierEnabled,
                                                 .bool_value = feature_scene_classifier_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    bool feature_ocr_enabled = panel_state.view.feature_ocr_enabled;
    if (ImGui::Checkbox("Enable OCR", &feature_ocr_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureOcrEnabled,
                                                 .bool_value = feature_ocr_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    bool feature_face_emotion_enabled = panel_state.view.feature_face_emotion_enabled;
    if (ImGui::Checkbox("Enable Face Emotion", &feature_face_emotion_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureFaceEmotionEnabled,
                                                 .bool_value = feature_face_emotion_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    bool feature_asr_enabled = panel_state.view.feature_asr_enabled;
    if (ImGui::Checkbox("Enable ASR", &feature_asr_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureAsrEnabled,
                                                 .bool_value = feature_asr_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
}

void RenderRuntimeEditorParamGroups(AppRuntime &runtime, const std::vector<int> &param_indices) {
    ImGui::SeparatorText("Selected Group Parameters");
    if (ImGui::BeginTable("selected_group_param_table",
                          6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Param", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.14f);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthStretch, 0.14f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableHeadersRow();

        for (int param_idx : param_indices) {
            if (param_idx < 0 || param_idx >= static_cast<int>(runtime.model.parameters.size())) {
                continue;
            }
            auto &model_param = runtime.model.parameters[static_cast<std::size_t>(param_idx)];
            auto &param = model_param.param;
            const auto &spec = param.spec();
            float target_value = param.target();

            ImGui::PushID(param_idx);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool is_selected = runtime.selected_param_index == param_idx;
            if (ImGui::Selectable(model_param.id.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                runtime.selected_param_index = param_idx;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", spec.min_value);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", spec.max_value);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", spec.default_value);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.3f", param.value());
            ImGui::TableSetColumnIndex(5);
            const float before_target_value = param.target();
            if (ImGui::SliderFloat("##target", &target_value, spec.min_value, spec.max_value, "%.3f")) {
                param.SetTarget(target_value);
                EditCommand cmd{};
                cmd.type = EditCommand::Type::Param;
                cmd.param_id = model_param.id;
                cmd.before_param_value = before_target_value;
                cmd.after_param_value = target_value;
                PushEditCommand(runtime.undo_stack, runtime.redo_stack, std::move(cmd));
                runtime.editor_project_dirty = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void RenderRuntimeEditorBatchBind(AppRuntime &runtime, const std::string &group_label, const std::vector<int> &param_indices) {
    const char *binding_props[] = {"PosX", "PosY", "RotDeg", "ScaleX", "ScaleY", "Opacity"};
    ImGui::Combo("Bind Property", &runtime.batch_bind_prop_type, binding_props, 6);
    runtime.batch_bind_prop_type = std::clamp(runtime.batch_bind_prop_type, 0, 5);

    ImGui::SliderFloat("Bind In Min", &runtime.batch_bind_in_min, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Bind In Max", &runtime.batch_bind_in_max, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Bind Out Min", &runtime.batch_bind_out_min, -180.0f, 180.0f, "%.2f");
    ImGui::SliderFloat("Bind Out Max", &runtime.batch_bind_out_max, -180.0f, 180.0f, "%.2f");

    const BindingType bt = static_cast<BindingType>(runtime.batch_bind_prop_type);
    if (ImGui::Button("Apply Batch Bind -> Selected Part")) {
        if (runtime.selected_part_index >= 0 &&
            runtime.selected_part_index < static_cast<int>(runtime.model.parts.size())) {
            auto &part = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)];
            for (int param_idx : param_indices) {
                desktoper2D::UpsertBinding(part,
                              param_idx,
                              bt,
                              runtime.batch_bind_in_min,
                              runtime.batch_bind_in_max,
                              runtime.batch_bind_out_min,
                              runtime.batch_bind_out_max);
            }
            runtime.editor_status = "batch bind applied to selected part: " + part.id +
                                    " | group=" + group_label +
                                    " | prop=" + desktoper2D::BindingTypeNameUi(bt);
            runtime.editor_status_ttl = 2.5f;
        } else {
            runtime.editor_status = "batch bind failed: no selected part";
            runtime.editor_status_ttl = 2.5f;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply Batch Bind -> All Parts")) {
        int touched = 0;
        for (auto &part : runtime.model.parts) {
            for (int param_idx : param_indices) {
                desktoper2D::UpsertBinding(part,
                              param_idx,
                              bt,
                              runtime.batch_bind_in_min,
                              runtime.batch_bind_in_max,
                              runtime.batch_bind_out_min,
                              runtime.batch_bind_out_max);
            }
            touched += 1;
        }
        runtime.editor_status = "batch bind applied to all parts=" + std::to_string(touched) +
                                " | group=" + group_label +
                                " | prop=" + desktoper2D::BindingTypeNameUi(bt);
        runtime.editor_status_ttl = 2.5f;
    }
}

}  // namespace

TimelineInteractionStorage &GetTimelineInteractionStorage() {
    static TimelineInteractionStorage storage{};
    return storage;
}



void RenderUnifiedPluginStatusCard(const AppRuntime &runtime, const char *empty_hint) {
    const UnifiedPluginEntry *active_unified_plugin = nullptr;
    if (runtime.unified_plugin_selected_index >= 0 &&
        runtime.unified_plugin_selected_index < static_cast<int>(runtime.unified_plugin_entries.size())) {
        active_unified_plugin = &runtime.unified_plugin_entries[static_cast<std::size_t>(runtime.unified_plugin_selected_index)];
    } else if (!runtime.unified_plugin_entries.empty()) {
        active_unified_plugin = &runtime.unified_plugin_entries.front();
    }

    if (active_unified_plugin != nullptr) {
        ImGui::SeparatorText(active_unified_plugin->name.c_str());
        ImGui::Text("Kind: %s", UnifiedPluginKindLabel(active_unified_plugin->kind));
        ImGui::SameLine();
        ImGui::TextColored(UnifiedPluginStatusColor(active_unified_plugin->status),
                           "Status: %s",
                           UnifiedPluginStatusLabel(active_unified_plugin->status));
        ImGui::Text("Primary Id: %s", active_unified_plugin->id.c_str());
        if (!active_unified_plugin->version.empty()) {
            ImGui::Text("Version: %s", active_unified_plugin->version.c_str());
        }
        if (!active_unified_plugin->backend.empty()) {
            ImGui::Text("Backend: %s", active_unified_plugin->backend.c_str());
        }
        if (!active_unified_plugin->source.empty()) {
            ImGui::TextWrapped("Source: %s", active_unified_plugin->source.c_str());
        }
        if (!active_unified_plugin->assets.empty()) {
            std::string assets_text = JoinAssetsUi(active_unified_plugin->assets);
            RenderLongTextBlock("Assets", "overview_unified_assets", &assets_text, 4, 70.0f);
        } else {
            ImGui::TextDisabled("(no assets)");
        }
    } else {
        RenderUnifiedEmptyState("overview_unified_plugin_empty_state",
                                "无插件数据",
                                (empty_hint && empty_hint[0] != '\0') ? empty_hint : "尚未发现 Unified Plugin 条目。",
                                ImVec4(0.72f, 0.82f, 1.0f, 1.0f));
    }
}

std::string &RuntimeOpsStatusStorage() {
    static std::string status;
    return status;
}


}  // namespace desktoper2D
