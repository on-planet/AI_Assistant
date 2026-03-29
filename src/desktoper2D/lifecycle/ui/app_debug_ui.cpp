#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
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
#include "desktoper2D/lifecycle/ui/commands/plugin_commands.h"
#include "desktoper2D/lifecycle/ui/ui_empty_state.h"

namespace desktoper2D {

namespace {

std::string TrimCopy(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> SplitExtraOnnxLines(const char *text) {
    std::vector<std::string> out;
    if (!text || text[0] == '\0') {
        return out;
    }
    std::string line;
    std::istringstream iss(text);
    while (std::getline(iss, line)) {
        const std::string trimmed = TrimCopy(line);
        if (!trimmed.empty()) {
            out.push_back(trimmed);
        }
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

void RenderRuntimeEditorFeatureToggles(AppRuntime &runtime) {
    const EditorPanelState &panel_state = BuildEditorPanelState(runtime);

    ImGui::SeparatorText("Feature Toggles");
    bool feature_scene_classifier_enabled = panel_state.view.feature_scene_classifier_enabled;
    if (ImGui::Checkbox("Enable Scene Classifier", &feature_scene_classifier_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureSceneClassifierEnabled,
                                                 .bool_value = feature_scene_classifier_enabled});
        (void)BuildEditorPanelState(runtime);
    }
    bool feature_ocr_enabled = panel_state.view.feature_ocr_enabled;
    if (ImGui::Checkbox("Enable OCR", &feature_ocr_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureOcrEnabled,
                                                 .bool_value = feature_ocr_enabled});
        (void)BuildEditorPanelState(runtime);
    }
    bool feature_face_emotion_enabled = panel_state.view.feature_face_emotion_enabled;
    if (ImGui::Checkbox("Enable Face Emotion", &feature_face_emotion_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureFaceEmotionEnabled,
                                                 .bool_value = feature_face_emotion_enabled});
        (void)BuildEditorPanelState(runtime);
    }
    bool feature_asr_enabled = panel_state.view.feature_asr_enabled;
    if (ImGui::Checkbox("Enable ASR", &feature_asr_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureAsrEnabled,
                                                 .bool_value = feature_asr_enabled});
        (void)BuildEditorPanelState(runtime);
    }
}

void RenderRuntimeEditorParamGroups(RuntimeUiView view, const std::vector<int> &param_indices) {
    AppRuntime &runtime = view.runtime;
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

void RenderRuntimeEditorBatchBind(RuntimeUiView view,
                                  const std::string &group_label,
                                  const std::vector<int> &param_indices) {
    AppRuntime &runtime = view.runtime;
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



void RenderRuntimePluginDetailPanel(RuntimeUiView view) {
    AppRuntime &runtime = view.runtime;
    ImGui::SeparatorText("Plugin Detail");
    if (ImGui::Button("Back to Cards")) {
        runtime.plugin.detail_kind = PluginDetailKind::None;
        runtime.workspace_ui.panels.show_plugin_detail_window = false;
        runtime.workspace_ui.panels.show_plugin_quick_control_window = true;
        return;
    }
    ImGui::Separator();

    ImGui::Text("Title: %s", runtime.plugin.detail_title.empty() ? "(none)" : runtime.plugin.detail_title.c_str());
    if (!runtime.plugin.detail_source.empty()) {
        ImGui::TextWrapped("Source: %s", runtime.plugin.detail_source.c_str());
    }
    if (!runtime.plugin.detail_backend.empty()) {
        ImGui::Text("Backend: %s", runtime.plugin.detail_backend.c_str());
    }
    if (!runtime.plugin.detail_status.empty()) {
        ImGui::Text("Status: %s", runtime.plugin.detail_status.c_str());
    }
    if (!runtime.plugin.detail_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Last Error: %s", runtime.plugin.detail_last_error.c_str());
    }
    if (!runtime.plugin.detail_assets.empty()) {
        std::string assets_text = JoinAssetsUi(runtime.plugin.detail_assets);
        RenderLongTextBlock("Assets", "plugin_detail_assets", &assets_text, 8, 160.0f);
    } else {
        ImGui::TextDisabled("(no assets)");
    }

    ImGui::SeparatorText("缂栬緫閰嶇疆");
    if (runtime.plugin.detail_source.empty()) {
        ImGui::TextDisabled("(no editable config)");
        return;
    }

    auto load_behavior_edit = [&]() -> bool {
        std::string model_id;
        std::string model_version;
        std::string onnx;
        std::vector<std::string> extra;
        std::string err;
        if (!LoadPluginConfigFields(runtime.plugin.detail_source, &model_id, &model_version, &onnx, &extra, &err)) {
            runtime.plugin.switch_status.clear();
            runtime.plugin.switch_error = err.empty() ? "load behavior config failed" : err;
            return false;
        }
        SDL_strlcpy(runtime.plugin.detail_edit_model_id_input, model_id.c_str(), sizeof(runtime.plugin.detail_edit_model_id_input));
        SDL_strlcpy(runtime.plugin.detail_edit_onnx_input, onnx.c_str(), sizeof(runtime.plugin.detail_edit_onnx_input));
        std::string extra_text;
        for (const auto &item : extra) {
            if (!extra_text.empty()) extra_text += "\n";
            extra_text += item;
        }
        SDL_strlcpy(runtime.plugin.detail_edit_extra_onnx_input, extra_text.c_str(), sizeof(runtime.plugin.detail_edit_extra_onnx_input));
        return true;
    };

    auto load_default_edit = [&]() -> bool {
        std::ifstream ifs(runtime.plugin.detail_source, std::ios::binary);
        if (!ifs) {
            runtime.plugin.switch_status.clear();
            runtime.plugin.switch_error = "load default config failed";
            return false;
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        const std::string text = oss.str();
        JsonParseError err{};
        auto root_opt = ParseJson(text, &err);
        if (!root_opt || !root_opt->isObject()) {
            runtime.plugin.switch_status.clear();
            runtime.plugin.switch_error = "parse default config failed";
            return false;
        }
        const JsonValue &root = *root_opt;
        auto read_string = [&](const char *key, char *buf, std::size_t len) {
            const auto value = root.getString(key).value_or(std::string());
            SDL_strlcpy(buf, value.c_str(), len);
        };
        read_string("onnx", runtime.plugin.detail_edit_onnx_input, sizeof(runtime.plugin.detail_edit_onnx_input));
        read_string("labels", runtime.plugin.detail_edit_labels_input, sizeof(runtime.plugin.detail_edit_labels_input));
        read_string("det", runtime.plugin.detail_edit_det_input, sizeof(runtime.plugin.detail_edit_det_input));
        read_string("rec", runtime.plugin.detail_edit_rec_input, sizeof(runtime.plugin.detail_edit_rec_input));
        read_string("keys", runtime.plugin.detail_edit_keys_input, sizeof(runtime.plugin.detail_edit_keys_input));
        read_string("model", runtime.plugin.detail_edit_model_input, sizeof(runtime.plugin.detail_edit_model_input));
        return true;
    };

    const bool is_behavior = runtime.plugin.detail_kind == PluginDetailKind::Behavior;
    if (!runtime.plugin.detail_edit_loaded || runtime.plugin.detail_edit_source != runtime.plugin.detail_source) {
        runtime.plugin.detail_edit_loaded = false;
        runtime.plugin.detail_edit_source = runtime.plugin.detail_source;
        const bool loaded = is_behavior ? load_behavior_edit() : load_default_edit();
        runtime.plugin.detail_edit_loaded = loaded;
    }

    if (!runtime.plugin.detail_edit_loaded) {
        ImGui::TextDisabled("(config not loaded)");
        return;
    }

    if (is_behavior) {
        ImGui::InputTextWithHint("Model Id", "model_id", runtime.plugin.detail_edit_model_id_input,
                                 sizeof(runtime.plugin.detail_edit_model_id_input));
        ImGui::InputTextWithHint("ONNX", "onnx path", runtime.plugin.detail_edit_onnx_input,
                                 sizeof(runtime.plugin.detail_edit_onnx_input));
        ImGui::InputTextMultiline("Extra ONNX (one per line)", runtime.plugin.detail_edit_extra_onnx_input,
                                  sizeof(runtime.plugin.detail_edit_extra_onnx_input), ImVec2(-1.0f, 80.0f));
        if (ImGui::Button("Save and Reload")) {
            std::string err;
            std::vector<std::string> extra = SplitExtraOnnxLines(runtime.plugin.detail_edit_extra_onnx_input);
            const bool ok = SavePluginConfigFields(runtime.plugin.detail_source,
                                                   runtime.plugin.detail_edit_model_id_input,
                                                   runtime.plugin.detail_edit_onnx_input,
                                                   extra,
                                                   &err);
            if (ok) {
                std::string reload_err;
                ReloadPluginByConfigPath(runtime, runtime.plugin.detail_source, &reload_err);
                runtime.plugin.switch_status = reload_err.empty() ? "plugin reloaded" : reload_err;
                runtime.plugin.switch_error.clear();
                runtime.plugin.config_refresh_requested = true;
            } else {
                runtime.plugin.switch_status.clear();
                runtime.plugin.switch_error = err.empty() ? "save failed" : err;
            }
        }
    } else {
        if (runtime.plugin.detail_kind == PluginDetailKind::Asr) {
            ImGui::InputTextWithHint("Model", "onnx", runtime.plugin.detail_edit_onnx_input,
                                     sizeof(runtime.plugin.detail_edit_onnx_input));
        } else if (runtime.plugin.detail_kind == PluginDetailKind::Ocr) {
            ImGui::InputTextWithHint("Det", "det", runtime.plugin.detail_edit_det_input,
                                     sizeof(runtime.plugin.detail_edit_det_input));
            ImGui::InputTextWithHint("Rec", "rec", runtime.plugin.detail_edit_rec_input,
                                     sizeof(runtime.plugin.detail_edit_rec_input));
            ImGui::InputTextWithHint("Keys", "keys", runtime.plugin.detail_edit_keys_input,
                                     sizeof(runtime.plugin.detail_edit_keys_input));
        } else if (runtime.plugin.detail_kind == PluginDetailKind::Scene || runtime.plugin.detail_kind == PluginDetailKind::Facemesh) {
            ImGui::InputTextWithHint("Model", "onnx", runtime.plugin.detail_edit_onnx_input,
                                     sizeof(runtime.plugin.detail_edit_onnx_input));
            ImGui::InputTextWithHint("Labels", "labels", runtime.plugin.detail_edit_labels_input,
                                     sizeof(runtime.plugin.detail_edit_labels_input));
        } else if (runtime.plugin.detail_kind == PluginDetailKind::Chat) {
            ImGui::InputTextWithHint("Model", "model", runtime.plugin.detail_edit_model_input,
                                     sizeof(runtime.plugin.detail_edit_model_input));
        }

        if (ImGui::Button("Save and Apply")) {
            std::ifstream ifs(runtime.plugin.detail_source, std::ios::binary);
            std::ostringstream oss;
            if (ifs) {
                oss << ifs.rdbuf();
            }
            JsonParseError err{};
            auto root_opt = ParseJson(oss.str(), &err);
            if (!root_opt || !root_opt->isObject()) {
                runtime.plugin.switch_status.clear();
                runtime.plugin.switch_error = "parse default config failed";
            } else {
                JsonValue root = *root_opt;
                JsonObject *obj = root.asObject();
                auto assign_string = [&](const char *key, const char *value) {
                    if (!obj) return;
                    if (value && value[0] != '\0') {
                        (*obj)[key] = JsonValue::makeString(value);
                    } else {
                        auto it = obj->find(key);
                        if (it != obj->end()) obj->erase(it);
                    }
                };
                assign_string("onnx", runtime.plugin.detail_edit_onnx_input);
                assign_string("labels", runtime.plugin.detail_edit_labels_input);
                assign_string("det", runtime.plugin.detail_edit_det_input);
                assign_string("rec", runtime.plugin.detail_edit_rec_input);
                assign_string("keys", runtime.plugin.detail_edit_keys_input);
                assign_string("model", runtime.plugin.detail_edit_model_input);

                std::ofstream ofs(runtime.plugin.detail_source, std::ios::binary);
                if (!ofs) {
                    runtime.plugin.switch_status.clear();
                    runtime.plugin.switch_error = "save default config failed";
                } else {
                    ofs << StringifyJson(root, 2);
                    ofs.close();
                    runtime.plugin.switch_status = "default plugin config saved";
                    runtime.plugin.switch_error.clear();
                    runtime.plugin.default_plugin_catalog_refresh_requested = true;

                    if (runtime.plugin.detail_kind == PluginDetailKind::Asr) {
                        runtime.plugin.override_asr_model_path = runtime.plugin.detail_edit_onnx_input;
                        ApplyPluginAction(runtime,
                                          PluginAction{
                                              .type = PluginActionType::ApplyOverrideModels,
                                              .feedback_slot = PluginActionFeedbackSlot::Switch,
                                              .text_value = "default plugin config saved",
                                              .text_value_2 = "apply override failed",
                                              .text_value_3 = "asr",
                                          });
                    } else if (runtime.plugin.detail_kind == PluginDetailKind::Ocr) {
                        runtime.plugin.override_ocr_det_path = runtime.plugin.detail_edit_det_input;
                        runtime.plugin.override_ocr_rec_path = runtime.plugin.detail_edit_rec_input;
                        runtime.plugin.override_ocr_keys_path = runtime.plugin.detail_edit_keys_input;
                        ApplyPluginAction(runtime,
                                          PluginAction{
                                              .type = PluginActionType::ApplyOverrideModels,
                                              .feedback_slot = PluginActionFeedbackSlot::Switch,
                                              .text_value = "default plugin config saved",
                                              .text_value_2 = "apply override failed",
                                              .text_value_3 = "ocr",
                                          });
                    } else if (runtime.plugin.detail_kind == PluginDetailKind::Scene) {
                        runtime.plugin.override_scene_model_path = runtime.plugin.detail_edit_onnx_input;
                        runtime.plugin.override_scene_labels_path = runtime.plugin.detail_edit_labels_input;
                        ApplyPluginAction(runtime,
                                          PluginAction{
                                              .type = PluginActionType::ApplyOverrideModels,
                                              .feedback_slot = PluginActionFeedbackSlot::Switch,
                                              .text_value = "default plugin config saved",
                                              .text_value_2 = "apply override failed",
                                              .text_value_3 = "scene",
                                          });
                    } else if (runtime.plugin.detail_kind == PluginDetailKind::Facemesh) {
                        runtime.plugin.override_facemesh_model_path = runtime.plugin.detail_edit_onnx_input;
                        runtime.plugin.override_facemesh_labels_path = runtime.plugin.detail_edit_labels_input;
                        ApplyPluginAction(runtime,
                                          PluginAction{
                                              .type = PluginActionType::ApplyOverrideModels,
                                              .feedback_slot = PluginActionFeedbackSlot::Switch,
                                              .text_value = "default plugin config saved",
                                              .text_value_2 = "apply override failed",
                                              .text_value_3 = "facemesh",
                                          });
                    } else {
                        runtime.plugin.unified_refresh_requested = true;
                    }
                }
            }
        }
    }

    if (!runtime.plugin.switch_status.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", runtime.plugin.switch_status.c_str());
    }
    if (!runtime.plugin.switch_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", runtime.plugin.switch_error.c_str());
    }
}

void RenderUnifiedPluginStatusCard(const AppRuntime &runtime, const char *empty_hint) {
    const UnifiedPluginEntry *active_unified_plugin = nullptr;
    if (runtime.plugin.unified_selected_index >= 0 &&
        runtime.plugin.unified_selected_index < static_cast<int>(runtime.plugin.unified_entries.size())) {
        active_unified_plugin = &runtime.plugin.unified_entries[static_cast<std::size_t>(runtime.plugin.unified_selected_index)];
    } else if (!runtime.plugin.unified_entries.empty()) {
        active_unified_plugin = &runtime.plugin.unified_entries.front();
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
                                "No unified plugin data",
                                (empty_hint && empty_hint[0] != '\0') ? empty_hint : "No unified plugin entries were discovered.",
                                ImVec4(0.72f, 0.82f, 1.0f, 1.0f));
    }
}

std::string &RuntimeOpsStatusStorage() {
    static std::string status;
    return status;
}


}  // namespace desktoper2D

