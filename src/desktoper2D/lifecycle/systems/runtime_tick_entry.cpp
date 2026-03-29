#include "desktoper2D/lifecycle/systems/runtime_tick_entry.h"

#include <vector>
#include <cmath>
#include <algorithm>

#include "desktoper2D/controllers/interaction_controller.h"
#include "desktoper2D/lifecycle/editor/editor_session_service.h"
#include "desktoper2D/core/model.h"
#include "desktoper2D/lifecycle/behavior_applier.h"
#include "desktoper2D/lifecycle/model_reload_service.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"
#include "desktoper2D/lifecycle/services/decision_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/services/task_category_service.h"
#include "desktoper2D/lifecycle/systems/app_systems.h"
#include "desktoper2D/lifecycle/ui/commands/ui_command_bridge.h"

namespace desktoper2D {

namespace {

void TickEditor(AppRuntime &runtime, EditorStateSlice editor, float dt, const RuntimeTickBridge &bridge) {
    if (!editor.editor.editor_autosave_recovery_checked) {
        RefreshEditorAutosaveState(runtime);
    }

    if (editor.editor.editor_autosave_enabled && editor.core.model_loaded) {
        if (!editor.editor.editor_project_dirty) {
            editor.editor.editor_autosave_accum_sec = 0.0f;
        } else {
            editor.editor.editor_autosave_accum_sec += std::max(0.0f, dt);
            if (editor.editor.editor_autosave_accum_sec >= editor.editor.editor_autosave_interval_sec) {
                std::string err;
                if (SaveEditorAutosaveProject(runtime, &err)) {
                    editor.editor.editor_status = "autosaved project";
                    editor.editor.editor_status_ttl = 2.0f;
                } else {
                    editor.editor.editor_status = "autosave failed: " + err;
                    editor.editor.editor_status_ttl = 3.5f;
                }
                editor.editor.editor_autosave_accum_sec = 0.0f;
            }
        }
    }

    if (!editor.core.model_loaded) {
        return;
    }

    if (bridge.build_model_reload_context) {
        auto reload_ctx = bridge.build_model_reload_context();
        TryHotReloadModel(reload_ctx, dt);
    }

    UpdateHeadPatReaction(
        editor.editor.interaction_state,
        InteractionControllerContext{
            .model_loaded = editor.core.model_loaded,
            .model = &editor.core.model,
            .pick_top_part_at = bridge.pick_top_part_at,
            .has_model_params = bridge.has_model_params,
        },
        dt);
    UpdateModelRuntime(&editor.core.model, editor.core.model_time, dt);
}

void TickDecision(PerceptionStateSlice perception, PluginStateSlice plugin, float dt) {
    RefreshTaskDecisionInputCache(perception.perception.perception_state);
    const OcrResult &ocr_for_decision = GetTaskDecisionOcrInput(perception.perception.perception_state);
    TickTaskDecision(perception.perception.perception_state.system_context_snapshot,
                     ocr_for_decision,
                     perception.perception.perception_state.scene_result,
                     perception.perception.perception_state.decision_signature.input_signature,
                     dt,
                     plugin.observability.task_decision.config,
                     plugin.observability.task_decision.schedule,
                     plugin.observability.task_decision.inference_state,
                     &perception.perception.perception_state.blackboard.asr.session_text,
                     plugin.observability.task_decision.last_result,
                     &plugin.observability.task_decision.error_info);
    plugin.observability.task_decision.primary = plugin.observability.task_decision.last_result.primary;
    plugin.observability.task_decision.secondary = plugin.observability.task_decision.last_result.secondary;
}

void TickBehavior(EditorStateSlice editor,
                  PerceptionStateSlice perception,
                  PluginStateSlice plugin,
                  float dt,
                  const RuntimeTickBridge &bridge) {
    if (!editor.core.model_loaded) {
        return;
    }

    if (perception.perception.feature_flags.face_param_mapping_enabled) {
        auto &fm = perception.perception.perception_state.face_emotion_result;

        perception.perception.face_map_gate_reason = "ok";
        perception.perception.face_map_sensor_fallback_active = false;
        perception.perception.face_map_sensor_fallback_reason = "none";
        perception.perception.face_map_raw_yaw_deg = fm.head_yaw_deg;
        perception.perception.face_map_raw_pitch_deg = fm.head_pitch_deg;
        perception.perception.face_map_raw_eye_open = fm.eye_open_avg;

        const float min_conf = std::clamp(perception.perception.face_map_min_confidence, 0.0f, 1.0f);
        const float deadzone_deg = std::max(0.0f, perception.perception.face_map_head_pose_deadzone_deg);
        const float yaw_max_deg = std::max(1.0f, perception.perception.face_map_yaw_max_deg);
        const float pitch_max_deg = std::max(1.0f, perception.perception.face_map_pitch_max_deg);
        const float eye_thr = std::clamp(perception.perception.face_map_eye_open_threshold, 0.0f, 1.0f);
        const float w = std::clamp(perception.perception.face_map_param_weight, 0.0f, 1.0f);
        const float smooth_alpha = std::clamp(perception.perception.face_map_smooth_alpha, 0.0f, 1.0f);

        bool gate_ok = true;
        bool fallback_requested = false;
        const char *fallback_reason = "none";

        if (!fm.face_detected) {
            perception.perception.face_map_gate_reason = "no_face";
            gate_ok = false;
            fallback_requested = true;
            fallback_reason = "no_face";
        } else if (fm.emotion_score < min_conf) {
            perception.perception.face_map_gate_reason = "low_conf";
            gate_ok = false;
            fallback_requested = true;
            fallback_reason = "low_conf";
        }

        if (!std::isfinite(fm.head_yaw_deg) || !std::isfinite(fm.head_pitch_deg) || !std::isfinite(fm.eye_open_avg)) {
            perception.perception.face_map_gate_reason = "nan_input";
            gate_ok = false;
            fallback_requested = true;
            fallback_reason = "nan_input";
        }

        if (perception.perception.perception_state.facemesh_error_info.code != RuntimeErrorCode::Ok) {
            fallback_requested = true;
            if (std::string(fallback_reason) == "none") {
                fallback_reason = "facemesh_error";
            }
        }

        float mapped_yaw = 0.0f;
        float mapped_pitch = 0.0f;
        float mapped_eye = std::clamp((fm.eye_open_avg - eye_thr) / std::max(1e-4f, 1.0f - eye_thr), 0.0f, 1.0f);

        if (gate_ok) {
            auto apply_deadzone = [&](float v_deg) {
                const float a = std::abs(v_deg);
                if (a <= deadzone_deg) return 0.0f;
                return (v_deg > 0.0f ? (a - deadzone_deg) : -(a - deadzone_deg));
            };
            const float yaw_dz = apply_deadzone(fm.head_yaw_deg);
            const float pitch_dz = apply_deadzone(fm.head_pitch_deg);

            mapped_yaw = std::clamp(yaw_dz / yaw_max_deg, -1.0f, 1.0f);
            mapped_pitch = std::clamp(pitch_dz / pitch_max_deg, -1.0f, 1.0f);
        } else {
            mapped_eye = 0.0f;
        }

        if (fallback_requested) {
            if (perception.perception.face_map_sensor_fallback_enabled) {
                const float fw = std::clamp(perception.perception.face_map_sensor_fallback_weight, 0.0f, 1.0f);
                const float fyaw = std::clamp(perception.perception.face_map_sensor_fallback_head_yaw, -1.0f, 1.0f);
                const float fpitch =
                    std::clamp(perception.perception.face_map_sensor_fallback_head_pitch, -1.0f, 1.0f);
                const float feye = std::clamp(perception.perception.face_map_sensor_fallback_eye_open, 0.0f, 1.0f);

                mapped_yaw = mapped_yaw * (1.0f - fw) + fyaw * fw;
                mapped_pitch = mapped_pitch * (1.0f - fw) + fpitch * fw;
                mapped_eye = mapped_eye * (1.0f - fw) + feye * fw;

                perception.perception.face_map_sensor_fallback_active = fw > 0.0f;
                perception.perception.face_map_sensor_fallback_reason = fallback_reason;
            } else {
                perception.perception.face_map_sensor_fallback_reason = "disabled";
            }
        }

        perception.perception.face_map_out_head_yaw =
            perception.perception.face_map_out_head_yaw * (1.0f - smooth_alpha) + mapped_yaw * smooth_alpha;
        perception.perception.face_map_out_head_pitch =
            perception.perception.face_map_out_head_pitch * (1.0f - smooth_alpha) + mapped_pitch * smooth_alpha;
        perception.perception.face_map_out_eye_open =
            perception.perception.face_map_out_eye_open * (1.0f - smooth_alpha) + mapped_eye * smooth_alpha;

        auto blend_target = [&](const char *pid, float norm, float min_value, float max_value) {
            const auto it = editor.core.model.param_index.find(pid);
            if (it == editor.core.model.param_index.end()) return;
            const int idx = it->second;
            if (idx < 0 || idx >= static_cast<int>(editor.core.model.parameters.size())) return;

            auto &param = editor.core.model.parameters[static_cast<std::size_t>(idx)].param;
            const float target = std::clamp(norm, -1.0f, 1.0f) * max_value;
            const float cur = param.target();
            const float mixed = cur * (1.0f - w) + target * w;
            param.SetTarget(std::clamp(mixed, min_value, max_value));
        };

        blend_target("HeadYaw", perception.perception.face_map_out_head_yaw, -1.0f, 1.0f);
        blend_target("HeadPitch", perception.perception.face_map_out_head_pitch, -1.0f, 1.0f);

        auto blend_eye = [&](const char *pid) {
            const auto it = editor.core.model.param_index.find(pid);
            if (it == editor.core.model.param_index.end()) return;
            const int idx = it->second;
            if (idx < 0 || idx >= static_cast<int>(editor.core.model.parameters.size())) return;

            auto &param = editor.core.model.parameters[static_cast<std::size_t>(idx)].param;
            const float target = std::clamp(perception.perception.face_map_out_eye_open, 0.0f, 1.0f);
            const float cur = param.target();
            const float mixed = cur * (1.0f - w) + target * w;
            param.SetTarget(std::clamp(mixed, param.spec().min_value, param.spec().max_value));
        };
        blend_eye("EyeOpen");
        blend_eye("EyeLOpen");
        blend_eye("EyeROpen");
    }

    BehaviorOutput local_out{};
    BuildInteractionBehaviorOutput(
        editor.editor.interaction_state,
        InteractionControllerContext{
            .model_loaded = editor.core.model_loaded,
            .model = &editor.core.model,
            .pick_top_part_at = bridge.pick_top_part_at,
            .has_model_params = bridge.has_model_params,
        },
        dt,
        local_out);

    BehaviorOutput plugin_out{};
    bool has_plugin_out = false;
    std::string next_route_selected = "unknown";
    double next_route_scene_score = 0.0;
    double next_route_task_score = 0.0;
    double next_route_presence_score = 0.0;
    double next_route_total_score = 0.0;
    std::vector<std::string> next_route_rejected_summary;
    if (plugin.plugin.plugin.ready && plugin.perception.feature_flags.plugin_enabled) {
        const TaskCategoryInferenceResult &task_result = plugin.observability.task_decision.last_result;

        PerceptionInput in{};
        in.time_sec = static_cast<double>(plugin.core.model_time);
        in.audio_level = 0.0f;
        in.user_presence = perception.perception.perception_state.face_emotion_result.face_detected
                               ? 1.0f
                               : (plugin.core.window_state.window_visible ? 1.0f : 0.0f);
        in.vision.user_presence = in.user_presence;
        in.vision.head_yaw_deg = perception.perception.perception_state.face_emotion_result.head_yaw_deg;
        in.vision.head_pitch_deg = perception.perception.perception_state.face_emotion_result.head_pitch_deg;
        in.vision.head_roll_deg = perception.perception.perception_state.face_emotion_result.head_roll_deg;
        in.vision.gaze_x = 0.0f;
        in.vision.gaze_y = 0.0f;
        in.state.window_visible = plugin.core.window_state.window_visible;
        in.state.click_through = plugin.core.window_state.click_through;
        in.state.manual_param_mode = plugin.ui.manual_param_mode;
        in.state.show_debug_stats = plugin.ui.show_debug_stats;
        in.state.dt_sec = dt;
        in.scene_label = perception.perception.perception_state.scene_result.label;
        in.task_label = bridge.TaskSecondaryCategoryName(plugin.observability.task_decision.secondary);
        in.routing.primary_label = TaskPrimaryCategoryName(task_result.primary);
        in.routing.primary_confidence = task_result.primary_confidence;
        in.routing.primary_structured_confidence = task_result.primary_structured_confidence;
        in.routing.secondary_label = TaskSecondaryCategoryName(task_result.secondary);
        in.routing.secondary_confidence = task_result.secondary_confidence;
        in.routing.secondary_structured_confidence = task_result.secondary_structured_confidence;
        in.routing.scene_confidence = task_result.scene_confidence;
        in.routing.source_scene_weight = task_result.source_scene_weight;
        in.routing.source_ocr_weight = task_result.source_ocr_weight;
        in.routing.source_context_weight = task_result.source_context_weight;
        in.routing.secondary_top_candidates.reserve(task_result.secondary_top_candidates.size());
        for (const auto &candidate : task_result.secondary_top_candidates) {
            in.routing.secondary_top_candidates.push_back(RoutingEvidenceCandidate{
                .label = TaskSecondaryCategoryName(candidate.category),
                .confidence = candidate.score,
            });
        }
        plugin.plugin.plugin.inference_adapter->SubmitInput(std::move(in));
        has_plugin_out = plugin.plugin.plugin.inference_adapter->TryConsumeLatestOutput(plugin_out, nullptr);
        if (has_plugin_out) {
            if (plugin_out.route_trace.valid) {
                next_route_selected = plugin_out.route_trace.selected_route;
                next_route_scene_score = plugin_out.route_trace.scene_score;
                next_route_task_score = plugin_out.route_trace.task_score;
                next_route_presence_score = plugin_out.route_trace.presence_score;
                next_route_total_score = plugin_out.route_trace.total_score;
                for (std::size_t i = 0; i < plugin_out.route_trace.rejected_count; ++i) {
                    const auto &rejected = plugin_out.route_trace.rejected[i];
                    next_route_rejected_summary.push_back(
                        rejected.name + " total=" + std::to_string(rejected.total_score) +
                        " scene=" + std::to_string(rejected.scene_score) +
                        " task=" + std::to_string(rejected.task_score) +
                        " presence=" + std::to_string(rejected.presence_score));
                }
            } else {
                static constexpr const char *kRoutePrefix = "plugin.route.";
                static constexpr const char *kTracePrefix = "plugin.route.trace.";
                static constexpr const char *kRejectedPrefix = "plugin.route.trace.rejected.";
                static constexpr const char *kSelectedKey = "plugin.route.selected";
                static constexpr const char *kSceneScoreKey = "plugin.route.trace.scene_score";
                static constexpr const char *kTaskScoreKey = "plugin.route.trace.task_score";
                static constexpr const char *kPresenceScoreKey = "plugin.route.trace.presence_score";
                static constexpr const char *kTotalScoreKey = "plugin.route.trace.total_score";

                std::string selected = "unknown";
                for (const auto &kv : plugin_out.event_scores) {
                    const auto &key = kv.first;
                    const float value = kv.second;

                    if (key == kSceneScoreKey) {
                        next_route_scene_score = value;
                        continue;
                    }
                    if (key == kTaskScoreKey) {
                        next_route_task_score = value;
                        continue;
                    }
                    if (key == kPresenceScoreKey) {
                        next_route_presence_score = value;
                        continue;
                    }
                    if (key == kTotalScoreKey) {
                        next_route_total_score = value;
                        continue;
                    }

                    if (key.rfind(kRejectedPrefix, 0) == 0) {
                        const bool has_score_suffix = key.find("scene_score") != std::string::npos ||
                                                      key.find("task_score") != std::string::npos ||
                                                      key.find("structured_score") != std::string::npos ||
                                                      key.find("presence_score") != std::string::npos;
                        if (!has_score_suffix) {
                            next_route_rejected_summary.push_back(key + "=" + std::to_string(value));
                        }
                        continue;
                    }

                    if (selected == "unknown" && value > 0.0f &&
                        key != kSelectedKey &&
                        key.rfind(kRoutePrefix, 0) == 0 &&
                        key.rfind(kTracePrefix, 0) != 0) {
                        selected = key.substr(std::char_traits<char>::length(kRoutePrefix));
                        continue;
                    }
                }
                next_route_selected = selected;
            }
        }
    } else if (!plugin.perception.feature_flags.plugin_enabled) {
        next_route_selected = "disabled";
    }

    auto &plugin_runtime = plugin.plugin.plugin;
    const bool route_state_changed = plugin_runtime.route_selected != next_route_selected ||
                                     plugin_runtime.route_scene_score != next_route_scene_score ||
                                     plugin_runtime.route_task_score != next_route_task_score ||
                                     plugin_runtime.route_presence_score != next_route_presence_score ||
                                     plugin_runtime.route_total_score != next_route_total_score ||
                                     plugin_runtime.route_rejected_summary != next_route_rejected_summary;
    plugin_runtime.route_selected = std::move(next_route_selected);
    plugin_runtime.route_scene_score = next_route_scene_score;
    plugin_runtime.route_task_score = next_route_task_score;
    plugin_runtime.route_presence_score = next_route_presence_score;
    plugin_runtime.route_total_score = next_route_total_score;
    plugin_runtime.route_rejected_summary = std::move(next_route_rejected_summary);
    if (route_state_changed) {
        plugin_runtime.panel_state_version += 1;
    }

    BehaviorMixResult mix_result{};
    std::vector<BehaviorMixSource> mix_sources;
    mix_sources.push_back(BehaviorMixSource{.name = "local_fsm", .output = &local_out, .global_weight = 1.0f});
    if (has_plugin_out) {
        mix_sources.push_back(BehaviorMixSource{.name = "plugin", .output = &plugin_out, .global_weight = 1.0f});
    }

    if (MixBehaviorOutputs(mix_sources, plugin.plugin.plugin.behavior_fusion_config, &mix_result) &&
        bridge.build_behavior_apply_context) {
        auto apply_ctx = bridge.build_behavior_apply_context();
        ApplyBehaviorOutput(mix_result.mixed, apply_ctx);
    }
}

void TickPerception(PerceptionStateSlice perception, PluginStateSlice plugin, OpsStateSlice ops, float dt) {
    TickAppSystems(perception, plugin, ops, dt);
}

}  // namespace

void RunRuntimeTickEntry(AppRuntime &runtime, float dt, const RuntimeTickBridge &bridge) {
    ConsumeUiCommandQueue(runtime);
    UpdatePluginLifecycle(runtime);

    RuntimeTickStateSlices slices = BuildRuntimeTickStateSlices(runtime);

    TickEditor(runtime, slices.editor, dt, bridge);

    if (slices.plugin.plugin.plugin.ready && slices.plugin.perception.feature_flags.plugin_enabled) {
        TickDecision(slices.perception, slices.plugin, dt);
    }

    TickBehavior(slices.editor, slices.perception, slices.plugin, dt, bridge);
    TickPerception(slices.perception, slices.plugin, slices.ops, dt);

    if (!slices.plugin.plugin.plugin.ready) {
        TickDecision(slices.perception, slices.plugin, dt);
    }
}

}  // namespace desktoper2D
