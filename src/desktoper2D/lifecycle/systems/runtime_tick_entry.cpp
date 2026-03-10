#include "desktoper2D/lifecycle/systems/runtime_tick_entry.h"

#include <vector>
#include <cmath>


#include "desktoper2D/controllers/interaction_controller.h"
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

void RunRuntimeTickEntry(AppRuntime &runtime, float dt, const RuntimeTickBridge &bridge) {
    ConsumeUiCommandQueue(runtime);
    UpdatePluginLifecycle(runtime);

    if (runtime.model_loaded) {
        if (bridge.build_model_reload_context) {
            auto reload_ctx = bridge.build_model_reload_context();
            TryHotReloadModel(reload_ctx, dt);
        }

        UpdateHeadPatReaction(
            runtime.interaction_state,
            InteractionControllerContext{
                .model_loaded = runtime.model_loaded,
                .model = &runtime.model,
                .pick_top_part_at = bridge.pick_top_part_at,
                .has_model_params = bridge.has_model_params,
            },
            dt);
        UpdateModelRuntime(&runtime.model, runtime.model_time, dt);
    }

    if (runtime.model_loaded && runtime.feature_face_param_mapping_enabled) {
        auto &fm = runtime.perception_state.face_emotion_result;

        runtime.face_map_gate_reason = "ok";
        runtime.face_map_sensor_fallback_active = false;
        runtime.face_map_sensor_fallback_reason = "none";
        runtime.face_map_raw_yaw_deg = fm.head_yaw_deg;
        runtime.face_map_raw_pitch_deg = fm.head_pitch_deg;
        runtime.face_map_raw_eye_open = fm.eye_open_avg;

        const float min_conf = std::clamp(runtime.face_map_min_confidence, 0.0f, 1.0f);
        const float deadzone_deg = std::max(0.0f, runtime.face_map_head_pose_deadzone_deg);
        const float yaw_max_deg = std::max(1.0f, runtime.face_map_yaw_max_deg);
        const float pitch_max_deg = std::max(1.0f, runtime.face_map_pitch_max_deg);
        const float eye_thr = std::clamp(runtime.face_map_eye_open_threshold, 0.0f, 1.0f);
        const float w = std::clamp(runtime.face_map_param_weight, 0.0f, 1.0f);
        const float smooth_alpha = std::clamp(runtime.face_map_smooth_alpha, 0.0f, 1.0f);

        bool gate_ok = true;
        bool fallback_requested = false;
        const char *fallback_reason = "none";

        if (!fm.face_detected) {
            runtime.face_map_gate_reason = "no_face";
            gate_ok = false;
            fallback_requested = true;
            fallback_reason = "no_face";
        } else if (fm.emotion_score < min_conf) {
            runtime.face_map_gate_reason = "low_conf";
            gate_ok = false;
            fallback_requested = true;
            fallback_reason = "low_conf";
        }

        if (!std::isfinite(fm.head_yaw_deg) || !std::isfinite(fm.head_pitch_deg) || !std::isfinite(fm.eye_open_avg)) {
            runtime.face_map_gate_reason = "nan_input";
            gate_ok = false;
            fallback_requested = true;
            fallback_reason = "nan_input";
        }

        if (runtime.perception_state.facemesh_error_info.code != RuntimeErrorCode::Ok) {
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
            if (runtime.face_map_sensor_fallback_enabled) {
                const float fw = std::clamp(runtime.face_map_sensor_fallback_weight, 0.0f, 1.0f);
                const float fyaw = std::clamp(runtime.face_map_sensor_fallback_head_yaw, -1.0f, 1.0f);
                const float fpitch = std::clamp(runtime.face_map_sensor_fallback_head_pitch, -1.0f, 1.0f);
                const float feye = std::clamp(runtime.face_map_sensor_fallback_eye_open, 0.0f, 1.0f);

                mapped_yaw = mapped_yaw * (1.0f - fw) + fyaw * fw;
                mapped_pitch = mapped_pitch * (1.0f - fw) + fpitch * fw;
                mapped_eye = mapped_eye * (1.0f - fw) + feye * fw;

                runtime.face_map_sensor_fallback_active = fw > 0.0f;
                runtime.face_map_sensor_fallback_reason = fallback_reason;
            } else {
                runtime.face_map_sensor_fallback_reason = "disabled";
            }
        }

        runtime.face_map_out_head_yaw = runtime.face_map_out_head_yaw * (1.0f - smooth_alpha) + mapped_yaw * smooth_alpha;
        runtime.face_map_out_head_pitch = runtime.face_map_out_head_pitch * (1.0f - smooth_alpha) + mapped_pitch * smooth_alpha;
        runtime.face_map_out_eye_open = runtime.face_map_out_eye_open * (1.0f - smooth_alpha) + mapped_eye * smooth_alpha;

        auto blend_target = [&](const char *pid, float norm, float min_value, float max_value) {
            const auto it = runtime.model.param_index.find(pid);
            if (it == runtime.model.param_index.end()) return;
            const int idx = it->second;
            if (idx < 0 || idx >= static_cast<int>(runtime.model.parameters.size())) return;

            auto &param = runtime.model.parameters[static_cast<std::size_t>(idx)].param;
            const float target = std::clamp(norm, -1.0f, 1.0f) * max_value;
            const float cur = param.target();
            const float mixed = cur * (1.0f - w) + target * w;
            param.SetTarget(std::clamp(mixed, min_value, max_value));
        };

        blend_target("HeadYaw", runtime.face_map_out_head_yaw, -1.0f, 1.0f);
        blend_target("HeadPitch", runtime.face_map_out_head_pitch, -1.0f, 1.0f);

        auto blend_eye = [&](const char *pid) {
            const auto it = runtime.model.param_index.find(pid);
            if (it == runtime.model.param_index.end()) return;
            const int idx = it->second;
            if (idx < 0 || idx >= static_cast<int>(runtime.model.parameters.size())) return;

            auto &param = runtime.model.parameters[static_cast<std::size_t>(idx)].param;
            const float target = std::clamp(runtime.face_map_out_eye_open, 0.0f, 1.0f);
            const float cur = param.target();
            const float mixed = cur * (1.0f - w) + target * w;
            param.SetTarget(std::clamp(mixed, param.spec().min_value, param.spec().max_value));
        };
        blend_eye("EyeOpen");
        blend_eye("EyeLOpen");
        blend_eye("EyeROpen");
    }

    if (runtime.model_loaded) {
        BehaviorOutput local_out{};
        BuildInteractionBehaviorOutput(
            runtime.interaction_state,
            InteractionControllerContext{
                .model_loaded = runtime.model_loaded,
                .model = &runtime.model,
                .pick_top_part_at = bridge.pick_top_part_at,
                .has_model_params = bridge.has_model_params,
            },
            dt,
            local_out);

        BehaviorOutput plugin_out{};
        bool has_plugin_out = false;
        runtime.plugin_route_selected = "unknown";
        runtime.plugin_route_scene_score = 0.0;
        runtime.plugin_route_task_score = 0.0;
        runtime.plugin_route_presence_score = 0.0;
        runtime.plugin_route_total_score = 0.0;
        runtime.plugin_route_rejected_summary.clear();
        if (runtime.plugin_ready && runtime.feature_plugin_enabled) {
            TaskCategoryInferenceResult task_result{};
            ComputeTaskDecision(runtime.perception_state.system_context_snapshot,
                                runtime.perception_state.ocr_result,
                                runtime.perception_state.scene_result,
                                runtime.task_category_config,
                                &runtime.asr_session_state.session_text,
                                task_result,
                                &runtime.decision_error_info);
            runtime.task_primary = task_result.primary;
            runtime.task_secondary = task_result.secondary;

            PerceptionInput in{};
            in.time_sec = static_cast<double>(runtime.model_time);
            in.audio_level = 0.0f;
            in.user_presence = runtime.perception_state.face_emotion_result.face_detected ? 1.0f : (runtime.window_visible ? 1.0f : 0.0f);
            in.vision.user_presence = in.user_presence;
            in.vision.head_yaw_deg = runtime.perception_state.face_emotion_result.head_yaw_deg;
            in.vision.head_pitch_deg = runtime.perception_state.face_emotion_result.head_pitch_deg;
            in.vision.head_roll_deg = runtime.perception_state.face_emotion_result.head_roll_deg;
            in.vision.gaze_x = 0.0f;
            in.vision.gaze_y = 0.0f;
            in.state.window_visible = runtime.window_visible;
            in.state.click_through = runtime.click_through;
            in.state.manual_param_mode = runtime.manual_param_mode;
            in.state.show_debug_stats = runtime.show_debug_stats;
            in.state.dt_sec = dt;
            in.scene_label = runtime.perception_state.scene_result.label;
            in.task_label = bridge.TaskSecondaryCategoryName(runtime.task_secondary);
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
            for (const auto &candidate : task_result.secondary_top_candidates) {
                in.routing.secondary_top_candidates.push_back(RoutingEvidenceCandidate{
                    .label = TaskSecondaryCategoryName(candidate.category),
                    .confidence = candidate.score,
                });
            }
            runtime.inference_adapter->SubmitInput(in);
            has_plugin_out = runtime.inference_adapter->TryConsumeLatestOutput(plugin_out, nullptr);
            if (has_plugin_out) {
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
                        runtime.plugin_route_scene_score = value;
                        continue;
                    }
                    if (key == kTaskScoreKey) {
                        runtime.plugin_route_task_score = value;
                        continue;
                    }
                    if (key == kPresenceScoreKey) {
                        runtime.plugin_route_presence_score = value;
                        continue;
                    }
                    if (key == kTotalScoreKey) {
                        runtime.plugin_route_total_score = value;
                        continue;
                    }

                    if (key.rfind(kRejectedPrefix, 0) == 0) {
                        const bool has_score_suffix = key.find("scene_score") != std::string::npos ||
                                                      key.find("task_score") != std::string::npos ||
                                                      key.find("structured_score") != std::string::npos ||
                                                      key.find("presence_score") != std::string::npos;
                        if (!has_score_suffix) {
                            runtime.plugin_route_rejected_summary.push_back(key + "=" + std::to_string(value));
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
                runtime.plugin_route_selected = selected;
            }
        } else if (!runtime.feature_plugin_enabled) {
            runtime.plugin_route_selected = "disabled";
        }

        BehaviorMixResult mix_result{};
        std::vector<BehaviorMixSource> mix_sources;
        mix_sources.push_back(BehaviorMixSource{.name = "local_fsm", .output = &local_out, .global_weight = 1.0f});
        if (has_plugin_out) {
            mix_sources.push_back(BehaviorMixSource{.name = "plugin", .output = &plugin_out, .global_weight = 1.0f});
        }

        if (MixBehaviorOutputs(mix_sources, runtime.behavior_fusion_config, &mix_result) && bridge.build_behavior_apply_context) {
            auto apply_ctx = bridge.build_behavior_apply_context();
            ApplyBehaviorOutput(mix_result.mixed, apply_ctx);
        }
    }

    TickAppSystems(runtime, dt);
    if (!runtime.plugin_ready) {
        bridge.InferTaskCategoryInplace();
    }
}

}  // namespace desktoper2D
