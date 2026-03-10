#include "desktoper2D/lifecycle/ui/app_debug_ui_actions.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"

#include <array>

namespace desktoper2D {

void ApplyTimelinePanelActionImpl(AppRuntime &runtime, const TimelinePanelAction &action) {
    auto has_params = [&]() {
        return !runtime.model.parameters.empty();
    };
    auto has_channels = [&]() {
        return !runtime.model.animation_channels.empty();
    };
    auto clamp_channel_index = [&]() {
        if (!has_channels()) {
            runtime.timeline_selected_channel_index = 0;
            return;
        }
        runtime.timeline_selected_channel_index = std::clamp(runtime.timeline_selected_channel_index,
                                                             0,
                                                             static_cast<int>(runtime.model.animation_channels.size()) - 1);
    };
    auto active_channel = [&]() -> AnimationChannel * {
        if (!has_channels()) {
            return nullptr;
        }
        clamp_channel_index();
        return &runtime.model.animation_channels[static_cast<std::size_t>(runtime.timeline_selected_channel_index)];
    };
    auto &interaction = GetTimelineInteractionStorage();

    switch (action.type) {
        case TimelinePanelActionType::SetEnabled:
            runtime.timeline_enabled = action.bool_value;
            runtime.model.animation_channels_enabled = runtime.timeline_enabled;
            break;
        case TimelinePanelActionType::SetCursorSec:
            runtime.timeline_cursor_sec = std::clamp(action.float_value,
                                                     0.0f,
                                                     std::max(0.1f, runtime.timeline_duration_sec));
            break;
        case TimelinePanelActionType::SetDurationSec:
            runtime.timeline_duration_sec = std::clamp(action.float_value, 0.5f, 30.0f);
            runtime.timeline_cursor_sec = std::clamp(runtime.timeline_cursor_sec,
                                                     0.0f,
                                                     std::max(0.1f, runtime.timeline_duration_sec));
            break;
        case TimelinePanelActionType::SetSnapEnabled:
            runtime.timeline_snap_enabled = action.bool_value;
            break;
        case TimelinePanelActionType::SetSnapMode:
            runtime.timeline_snap_mode = std::clamp(action.int_value, 0, 2);
            break;
        case TimelinePanelActionType::SetSnapFps:
            runtime.timeline_snap_fps = std::clamp(action.float_value, 1.0f, 120.0f);
            break;
        case TimelinePanelActionType::SelectChannel:
            runtime.timeline_selected_channel_index = action.int_value;
            clamp_channel_index();
            break;
        case TimelinePanelActionType::AddChannel: {
            if (!has_params()) {
                break;
            }
            const int pidx = runtime.selected_param_index >= 0 &&
                             runtime.selected_param_index < static_cast<int>(runtime.model.parameters.size())
                                 ? runtime.selected_param_index
                                 : 0;
            AnimationChannel ch{};
            ch.id = "timeline_" + runtime.model.parameters[static_cast<std::size_t>(pidx)].id;
            ch.param_index = pidx;
            ch.enabled = true;
            ch.weight = 1.0f;
            ch.blend = AnimationBlendMode::Override;
            ch.timeline_interp = TimelineInterpolation::Linear;
            runtime.model.animation_channels.push_back(std::move(ch));
            runtime.timeline_selected_channel_index = static_cast<int>(runtime.model.animation_channels.size()) - 1;
            break;
        }
        case TimelinePanelActionType::DeleteChannel: {
            if (!has_channels()) {
                break;
            }
            runtime.model.animation_channels.erase(runtime.model.animation_channels.begin() + runtime.timeline_selected_channel_index);
            runtime.timeline_selected_channel_index = std::max(0, runtime.timeline_selected_channel_index - 1);
            runtime.timeline_selected_keyframe_indices.clear();
            runtime.timeline_selected_keyframe_ids.clear();
            clamp_channel_index();
            break;
        }
        case TimelinePanelActionType::SetChannelEnabled: {
            if (auto *ch = active_channel()) {
                ch->enabled = action.bool_value;
            }
            break;
        }
        case TimelinePanelActionType::SetChannelInterpolation: {
            if (auto *ch = active_channel()) {
                const int idx = std::clamp(action.int_value, 0, 2);
                ch->timeline_interp = idx == 0 ? TimelineInterpolation::Step :
                                     (idx == 1 ? TimelineInterpolation::Linear : TimelineInterpolation::Hermite);
            }
            break;
        }
        case TimelinePanelActionType::SetChannelWrap: {
            if (auto *ch = active_channel()) {
                const int idx = std::clamp(action.int_value, 0, 2);
                ch->timeline_wrap = idx == 0 ? TimelineWrapMode::Clamp :
                                    (idx == 1 ? TimelineWrapMode::Loop : TimelineWrapMode::PingPong);
            }
            break;
        }
        case TimelinePanelActionType::SetChannelTargetParam: {
            if (auto *ch = active_channel(); ch && has_params()) {
                ch->param_index = std::clamp(action.int_value, 0, static_cast<int>(runtime.model.parameters.size()) - 1);
            }
            break;
        }
        case TimelinePanelActionType::AddOrUpdateKeyframeAtCursor: {
            auto *ch = active_channel();
            if (!ch || !has_params()) {
                break;
            }
            const int param_idx = std::clamp(ch->param_index, 0, static_cast<int>(runtime.model.parameters.size()) - 1);
            const auto before_keyframes = ch->keyframes;
            const auto before_selected_ids = runtime.timeline_selected_keyframe_ids;
            const float v = runtime.model.parameters[static_cast<std::size_t>(param_idx)].param.target();
            UpsertTimelineKeyframe(*ch, SnapTimelineTime(runtime, runtime.timeline_cursor_sec), v);
            NormalizeTimelineSelection(runtime, *ch);
            PushTimelineEditCommand(runtime, *ch, before_keyframes, ch->keyframes, before_selected_ids, runtime.timeline_selected_keyframe_ids);
            break;
        }
        case TimelinePanelActionType::CopySelectedKeyframes: {
            auto *ch = active_channel();
            if (!ch) {
                break;
            }
            runtime.timeline_keyframe_clipboard.clear();
            for (int idx : runtime.timeline_selected_keyframe_indices) {
                if (idx >= 0 && idx < static_cast<int>(ch->keyframes.size())) {
                    runtime.timeline_keyframe_clipboard.push_back(ch->keyframes[static_cast<std::size_t>(idx)]);
                }
            }
            if (!runtime.timeline_keyframe_clipboard.empty()) {
                std::sort(runtime.timeline_keyframe_clipboard.begin(),
                          runtime.timeline_keyframe_clipboard.end(),
                          [](const TimelineKeyframe &a, const TimelineKeyframe &b) { return a.time_sec < b.time_sec; });
            }
            break;
        }
        case TimelinePanelActionType::PasteAtCursor: {
            auto *ch = active_channel();
            if (!ch || runtime.timeline_keyframe_clipboard.empty()) {
                break;
            }
            const auto before_keyframes = ch->keyframes;
            const auto before_selected_ids = runtime.timeline_selected_keyframe_ids;
            const float base_time = runtime.timeline_keyframe_clipboard.front().time_sec;
            runtime.timeline_selected_keyframe_indices.clear();
            for (const auto &src_kf : runtime.timeline_keyframe_clipboard) {
                TimelineKeyframe pasted = src_kf;
                pasted.time_sec = SnapTimelineTime(runtime, runtime.timeline_cursor_sec + (src_kf.time_sec - base_time));
                bool updated = false;
                for (auto &dst_kf : ch->keyframes) {
                    if (std::abs(dst_kf.time_sec - pasted.time_sec) < 1e-4f) {
                        dst_kf = pasted;
                        updated = true;
                        break;
                    }
                }
                if (!updated) {
                    ch->keyframes.push_back(pasted);
                }
            }
            std::sort(ch->keyframes.begin(), ch->keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
                return a.time_sec < b.time_sec;
            });
            for (std::size_t i = 0; i < ch->keyframes.size(); ++i) {
                for (const auto &src_kf : runtime.timeline_keyframe_clipboard) {
                    const float pasted_time = SnapTimelineTime(runtime, runtime.timeline_cursor_sec + (src_kf.time_sec - base_time));
                    if (std::abs(ch->keyframes[i].time_sec - pasted_time) < 1e-4f) {
                        runtime.timeline_selected_keyframe_indices.push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
            NormalizeTimelineSelection(runtime, *ch);
            PushTimelineEditCommand(runtime, *ch, before_keyframes, ch->keyframes, before_selected_ids, runtime.timeline_selected_keyframe_ids);
            break;
        }
        case TimelinePanelActionType::RemoveLastKeyframe: {
            auto *ch = active_channel();
            if (!ch || ch->keyframes.empty()) {
                break;
            }
            const auto before_keyframes = ch->keyframes;
            const auto before_selected_ids = runtime.timeline_selected_keyframe_ids;
            ch->keyframes.pop_back();
            NormalizeTimelineSelection(runtime, *ch);
            PushTimelineEditCommand(runtime, *ch, before_keyframes, ch->keyframes, before_selected_ids, runtime.timeline_selected_keyframe_ids);
            break;
        }
        case TimelinePanelActionType::Undo:
            UndoLastEdit(runtime);
            break;
        case TimelinePanelActionType::Redo:
            RedoLastEdit(runtime);
            break;
        case TimelinePanelActionType::BeginTrackDrag: {
            auto *ch = active_channel();
            if (!ch) {
                break;
            }
            NormalizeTimelineSelection(runtime, *ch);
            interaction.dragging_keyframe_index = action.int_value;
            interaction.dragging_channel_index = runtime.timeline_selected_channel_index;
            interaction.drag_undo_snapshot = ch->keyframes;
            interaction.drag_undo_selected_ids = runtime.timeline_selected_keyframe_ids;
            interaction.drag_snapshot_captured = true;
            break;
        }
        case TimelinePanelActionType::UpdateTrackDrag: {
            auto *ch = active_channel();
            if (!ch || !interaction.drag_snapshot_captured) {
                break;
            }
            if (action.int_value < 0 || action.int_value >= static_cast<int>(interaction.drag_undo_snapshot.size())) {
                interaction.drag_snapshot_captured = false;
                interaction.dragging_keyframe_index = -1;
                interaction.dragging_channel_index = -1;
                interaction.drag_undo_snapshot.clear();
                interaction.drag_undo_selected_ids.clear();
                break;
            }

            float vmin = -1.0f;
            float vmax = 1.0f;
            if (!runtime.model.parameters.empty()) {
                const int param_idx = std::clamp(ch->param_index, 0, static_cast<int>(runtime.model.parameters.size()) - 1);
                const auto &spec = runtime.model.parameters[static_cast<std::size_t>(param_idx)].param.spec();
                vmin = std::min(spec.min_value, spec.max_value);
                vmax = std::max(spec.min_value, spec.max_value);
                if (std::abs(vmax - vmin) < 1e-6f) {
                    vmax = vmin + 1.0f;
                }
            }

            const auto &anchor = interaction.drag_undo_snapshot[static_cast<std::size_t>(action.int_value)];
            const float target_t = SnapTimelineTime(runtime, action.float_value);
            const float target_v = std::clamp(action.float_value2, vmin, vmax);
            const float delta_t = target_t - anchor.time_sec;
            const float delta_v = target_v - anchor.value;

            ch->keyframes = interaction.drag_undo_snapshot;
            for (int selected_idx : runtime.timeline_selected_keyframe_indices) {
                if (selected_idx < 0 || selected_idx >= static_cast<int>(ch->keyframes.size()) ||
                    selected_idx >= static_cast<int>(interaction.drag_undo_snapshot.size())) {
                    continue;
                }
                const auto &src_kf = interaction.drag_undo_snapshot[static_cast<std::size_t>(selected_idx)];
                auto &kf = ch->keyframes[static_cast<std::size_t>(selected_idx)];
                kf.time_sec = SnapTimelineTime(runtime, src_kf.time_sec + delta_t);
                kf.value = std::clamp(src_kf.value + delta_v, vmin, vmax);
            }
            std::vector<TimelineKeyframe> selected_after_drag;
            selected_after_drag.reserve(runtime.timeline_selected_keyframe_indices.size());
            for (int selected_idx : runtime.timeline_selected_keyframe_indices) {
                if (selected_idx < 0 || selected_idx >= static_cast<int>(ch->keyframes.size())) {
                    continue;
                }
                selected_after_drag.push_back(ch->keyframes[static_cast<std::size_t>(selected_idx)]);
            }
            std::sort(ch->keyframes.begin(), ch->keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
                return a.time_sec < b.time_sec;
            });
            runtime.timeline_selected_keyframe_indices.clear();
            for (std::size_t i = 0; i < ch->keyframes.size(); ++i) {
                for (const auto &selected_kf : selected_after_drag) {
                    if (TimelineKeyframeNearlyEqual(ch->keyframes[i], selected_kf)) {
                        runtime.timeline_selected_keyframe_indices.push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
            NormalizeTimelineSelection(runtime, *ch);
            break;
        }
        case TimelinePanelActionType::EndTrackDrag: {
            auto *ch = active_channel();
            if (!ch || !interaction.drag_snapshot_captured) {
                break;
            }
            PushTimelineEditCommand(runtime,
                                    *ch,
                                    interaction.drag_undo_snapshot,
                                    ch->keyframes,
                                    interaction.drag_undo_selected_ids,
                                    runtime.timeline_selected_keyframe_ids);
            interaction.drag_snapshot_captured = false;
            interaction.dragging_keyframe_index = -1;
            interaction.dragging_channel_index = -1;
            interaction.drag_undo_snapshot.clear();
            interaction.drag_undo_selected_ids.clear();
            break;
        }
        case TimelinePanelActionType::BeginBoxSelect:
            interaction.dragging_keyframe_index = -1;
            interaction.dragging_channel_index = -1;
            interaction.box_select_active = true;
            interaction.box_select_start_x = action.float_value;
            interaction.box_select_start_y = action.float_value2;
            interaction.box_select_end_x = action.float_value;
            interaction.box_select_end_y = action.float_value2;
            if (!action.additive) {
                runtime.timeline_selected_keyframe_indices.clear();
                runtime.timeline_selected_keyframe_ids.clear();
            }
            break;
        case TimelinePanelActionType::UpdateBoxSelect:
            if (interaction.box_select_active) {
                interaction.box_select_end_x = action.float_value;
                interaction.box_select_end_y = action.float_value2;
            }
            break;
        case TimelinePanelActionType::EndBoxSelect: {
            auto *ch = active_channel();
            if (!ch || !interaction.box_select_active) {
                interaction.box_select_active = false;
                break;
            }
            const float sel_min_x = std::min(interaction.box_select_start_x, interaction.box_select_end_x);
            const float sel_max_x = std::max(interaction.box_select_start_x, interaction.box_select_end_x);
            const float sel_min_y = std::min(interaction.box_select_start_y, interaction.box_select_end_y);
            const float sel_max_y = std::max(interaction.box_select_start_y, interaction.box_select_end_y);
            const float track_pos_x = action.float_value;
            const float track_pos_y = action.float_value2;
            const float track_w = static_cast<float>(action.int_value);
            const float track_h = static_cast<float>(action.int_value2);
            const float duration = std::max(0.001f, runtime.timeline_duration_sec);
            float vmin = -1.0f;
            float vmax = 1.0f;
            if (!runtime.model.parameters.empty()) {
                const int param_idx = std::clamp(ch->param_index, 0, static_cast<int>(runtime.model.parameters.size()) - 1);
                const auto &spec = runtime.model.parameters[static_cast<std::size_t>(param_idx)].param.spec();
                vmin = std::min(spec.min_value, spec.max_value);
                vmax = std::max(spec.min_value, spec.max_value);
                if (std::abs(vmax - vmin) < 1e-6f) {
                    vmax = vmin + 1.0f;
                }
            }
            auto time_to_x = [&](float t) {
                const float nt = std::clamp(t / duration, 0.0f, 1.0f);
                return track_pos_x + nt * track_w;
            };
            auto value_to_y = [&](float v) {
                const float nv = std::clamp((v - vmin) / std::max(1e-6f, vmax - vmin), 0.0f, 1.0f);
                return track_pos_y + (1.0f - nv) * track_h;
            };
            for (std::size_t i = 0; i < ch->keyframes.size(); ++i) {
                const auto &kf = ch->keyframes[i];
                const ImVec2 p(time_to_x(kf.time_sec), value_to_y(kf.value));
                if (p.x >= sel_min_x && p.x <= sel_max_x && p.y >= sel_min_y && p.y <= sel_max_y) {
                    if (std::find(runtime.timeline_selected_keyframe_indices.begin(),
                                  runtime.timeline_selected_keyframe_indices.end(),
                                  static_cast<int>(i)) == runtime.timeline_selected_keyframe_indices.end()) {
                        runtime.timeline_selected_keyframe_indices.push_back(static_cast<int>(i));
                    }
                }
            }
            NormalizeTimelineSelection(runtime, *ch);
            interaction.box_select_active = false;
            break;
        }
        case TimelinePanelActionType::SelectKeyframe: {
            auto *ch = active_channel();
            if (!ch) {
                break;
            }
            EnsureTimelineKeyframeStableIds(*ch);

            if (action.int_value < 0 || action.int_value >= static_cast<int>(ch->keyframes.size())) {
                interaction.dragging_keyframe_index = -1;
                interaction.dragging_channel_index = -1;
                break;
            }

            const int idx = action.int_value;
            const auto selected_it = std::find(runtime.timeline_selected_keyframe_indices.begin(),
                                               runtime.timeline_selected_keyframe_indices.end(),
                                               idx);
            const bool already_selected = selected_it != runtime.timeline_selected_keyframe_indices.end();

            if (!action.additive) {
                runtime.timeline_selected_keyframe_indices.clear();
                runtime.timeline_selected_keyframe_ids.clear();
                runtime.timeline_selected_keyframe_indices.push_back(idx);
                interaction.dragging_keyframe_index = idx;
                interaction.dragging_channel_index = runtime.timeline_selected_channel_index;
            } else if (already_selected) {
                runtime.timeline_selected_keyframe_indices.erase(selected_it);
                interaction.dragging_keyframe_index = -1;
                interaction.dragging_channel_index = -1;
            } else {
                runtime.timeline_selected_keyframe_indices.push_back(idx);
                interaction.dragging_keyframe_index = idx;
                interaction.dragging_channel_index = runtime.timeline_selected_channel_index;
            }

            NormalizeTimelineSelection(runtime, *ch);
            break;
        }
        case TimelinePanelActionType::SetKeyframeInTangent:
        case TimelinePanelActionType::SetKeyframeOutTangent:
        case TimelinePanelActionType::SetKeyframeInWeight:
        case TimelinePanelActionType::SetKeyframeOutWeight: {
            auto *ch = active_channel();
            if (!ch) {
                break;
            }
            EnsureTimelineKeyframeStableIds(*ch);
            const auto before_keyframes = ch->keyframes;
            const auto before_selected_ids = runtime.timeline_selected_keyframe_ids;
            bool changed = false;
            for (auto &kf : ch->keyframes) {
                if (kf.stable_id != action.stable_id) {
                    continue;
                }
                switch (action.type) {
                    case TimelinePanelActionType::SetKeyframeInTangent:
                        kf.in_tangent = action.float_value;
                        changed = true;
                        break;
                    case TimelinePanelActionType::SetKeyframeOutTangent:
                        kf.out_tangent = action.float_value;
                        changed = true;
                        break;
                    case TimelinePanelActionType::SetKeyframeInWeight:
                        kf.in_weight = std::clamp(action.float_value, 0.0f, 1.0f);
                        changed = true;
                        break;
                    case TimelinePanelActionType::SetKeyframeOutWeight:
                        kf.out_weight = std::clamp(action.float_value, 0.0f, 1.0f);
                        changed = true;
                        break;
                    default:
                        break;
                }
                break;
            }
            if (changed) {
                PushTimelineEditCommand(runtime,
                                        *ch,
                                        before_keyframes,
                                        ch->keyframes,
                                        before_selected_ids,
                                        runtime.timeline_selected_keyframe_ids);
            }
            break;
        }
        default:
            break;
    }
}

void ApplyEditorPanelActionImpl(AppRuntime &runtime, const EditorPanelAction &action) {
    switch (action.type) {
        case EditorPanelActionType::SetShowDebugStats:
            runtime.show_debug_stats = action.bool_value;
            break;
        case EditorPanelActionType::SetManualParamMode:
            runtime.manual_param_mode = action.bool_value;
            if (runtime.model_loaded) {
                runtime.model.animation_channels_enabled = !runtime.manual_param_mode;
                if (runtime.manual_param_mode) {
                    for (ModelParameter &p : runtime.model.parameters) {
                        p.param.SetTarget(p.param.value());
                    }
                }
            }
            break;
        case EditorPanelActionType::SetHairSpringEnabled:
            runtime.model.enable_hair_spring = action.bool_value;
            break;
        case EditorPanelActionType::SetSimpleMaskEnabled:
            runtime.model.enable_simple_mask = action.bool_value;
            break;
        case EditorPanelActionType::SetFeatureSceneClassifierEnabled:
            runtime.feature_scene_classifier_enabled = action.bool_value;
            break;
        case EditorPanelActionType::SetFeatureOcrEnabled:
            runtime.feature_ocr_enabled = action.bool_value;
            break;
        case EditorPanelActionType::SetFeatureFaceEmotionEnabled:
            runtime.feature_face_emotion_enabled = action.bool_value;
            break;
        case EditorPanelActionType::SetFeatureAsrEnabled:
            runtime.feature_asr_enabled = action.bool_value;
            break;
        case EditorPanelActionType::SetFeaturePluginEnabled:
            runtime.feature_plugin_enabled = action.bool_value;
            break;
        case EditorPanelActionType::SetParamGroupMode:
            runtime.param_group_mode = std::clamp(action.int_value, 0, 1);
            runtime.selected_param_group_index = 0;
            break;
        case EditorPanelActionType::SetParamSearch:
            SDL_strlcpy(runtime.param_search, action.text_value.c_str(), sizeof(runtime.param_search));
            runtime.selected_param_group_index = 0;
            break;
        case EditorPanelActionType::SelectParamGroup:
            runtime.selected_param_group_index = std::max(0, action.int_value);
            break;
        case EditorPanelActionType::SelectParam:
            runtime.selected_param_index = action.int_value;
            break;
        case EditorPanelActionType::SetParamTargetValue:
            if (action.int_value >= 0 && action.int_value < static_cast<int>(runtime.model.parameters.size())) {
                auto &model_param = runtime.model.parameters[static_cast<std::size_t>(action.int_value)];
                auto &param = model_param.param;
                const float before_target_value = param.target();
                const auto &spec = param.spec();
                const float target_value = std::clamp(action.float_value, spec.min_value, spec.max_value);
                param.SetTarget(target_value);
                EditCommand cmd{};
                cmd.type = EditCommand::Type::Param;
                cmd.param_id = model_param.id;
                cmd.before_param_value = before_target_value;
                cmd.after_param_value = target_value;
                PushEditCommand(runtime.undo_stack, runtime.redo_stack, std::move(cmd));
                runtime.editor_project_dirty = true;
            }
            break;
        case EditorPanelActionType::SetBatchBindPropType:
            runtime.batch_bind_prop_type = std::clamp(action.int_value, 0, 5);
            break;
        case EditorPanelActionType::SetBatchBindInMin:
            runtime.batch_bind_in_min = action.float_value;
            break;
        case EditorPanelActionType::SetBatchBindInMax:
            runtime.batch_bind_in_max = action.float_value;
            break;
        case EditorPanelActionType::SetBatchBindOutMin:
            runtime.batch_bind_out_min = action.float_value;
            break;
        case EditorPanelActionType::SetBatchBindOutMax:
            runtime.batch_bind_out_max = action.float_value;
            break;
        case EditorPanelActionType::ApplyBatchBindToSelectedPart:
        case EditorPanelActionType::ApplyBatchBindToAllParts: {
            const std::vector<ParamGroup> groups = BuildParamGroups(runtime, runtime.param_group_mode, runtime.param_search);
            if (groups.empty()) {
                runtime.editor_status = "batch bind failed: no parameter group";
                runtime.editor_status_ttl = 2.5f;
                break;
            }
            const int group_index = std::clamp(runtime.selected_param_group_index, 0, static_cast<int>(groups.size()) - 1);
            const auto &selected_group = groups[static_cast<std::size_t>(group_index)];
            const BindingType bt = static_cast<BindingType>(std::clamp(runtime.batch_bind_prop_type, 0, 5));
            if (action.type == EditorPanelActionType::ApplyBatchBindToSelectedPart) {
                if (runtime.selected_part_index >= 0 && runtime.selected_part_index < static_cast<int>(runtime.model.parts.size())) {
                    auto &part = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)];
                    for (int param_idx : selected_group.second) {
                        UpsertBinding(part,
                                      param_idx,
                                      bt,
                                      runtime.batch_bind_in_min,
                                      runtime.batch_bind_in_max,
                                      runtime.batch_bind_out_min,
                                      runtime.batch_bind_out_max);
                    }
                    runtime.editor_status = "batch bind applied to selected part: " + part.id +
                                            " | group=" + selected_group.first +
                                            " | prop=" + BindingTypeNameUi(bt);
                    runtime.editor_status_ttl = 2.5f;
                } else {
                    runtime.editor_status = "batch bind failed: no selected part";
                    runtime.editor_status_ttl = 2.5f;
                }
            } else {
                int touched = 0;
                for (auto &part : runtime.model.parts) {
                    for (int param_idx : selected_group.second) {
                        UpsertBinding(part,
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
                                        " | group=" + selected_group.first +
                                        " | prop=" + BindingTypeNameUi(bt);
                runtime.editor_status_ttl = 2.5f;
            }
            break;
        }
    }
}

TimelineInteractionSnapshot GetTimelineInteractionSnapshot() {
    const auto &interaction = GetTimelineInteractionStorage();
    TimelineInteractionSnapshot snapshot{};
    snapshot.drag_snapshot_captured = interaction.drag_snapshot_captured;
    snapshot.dragging_keyframe_index = interaction.dragging_keyframe_index;
    snapshot.dragging_channel_index = interaction.dragging_channel_index;
    snapshot.box_select_active = interaction.box_select_active;
    snapshot.box_select_start_x = interaction.box_select_start_x;
    snapshot.box_select_start_y = interaction.box_select_start_y;
    snapshot.box_select_end_x = interaction.box_select_end_x;
    snapshot.box_select_end_y = interaction.box_select_end_y;
    return snapshot;
}

void ApplyTimelinePanelAction(AppRuntime &runtime, const TimelinePanelAction &action) {
    ApplyTimelinePanelActionImpl(runtime, action);
}

void ApplyEditorPanelAction(AppRuntime &runtime, const EditorPanelAction &action) {
    ApplyEditorPanelActionImpl(runtime, action);
}


}  // namespace desktoper2D
