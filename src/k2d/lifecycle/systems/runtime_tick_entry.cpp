#include "k2d/lifecycle/systems/runtime_tick_entry.h"

#include <vector>
#include <cmath>

#include "k2d/controllers/interaction_controller.h"
#include "k2d/core/model.h"
#include "k2d/lifecycle/behavior_applier.h"
#include "k2d/lifecycle/model_reload_service.h"
#include "k2d/lifecycle/plugin_lifecycle.h"
#include "k2d/lifecycle/systems/app_systems.h"

namespace k2d {

void RunRuntimeTickEntry(AppRuntime &runtime, float dt, const RuntimeTickBridge &bridge) {
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
        if (!fm.face_detected) {
            runtime.face_map_gate_reason = "no_face";
            gate_ok = false;
        } else if (fm.emotion_score < min_conf) {
            runtime.face_map_gate_reason = "low_conf";
            gate_ok = false;
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
        if (runtime.plugin_ready) {
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
            in.scene_label = runtime.perception_state.scene_result.label;
            in.task_label = bridge.TaskSecondaryCategoryName(runtime.task_secondary);
            runtime.inference_adapter->SubmitInput(in);
            has_plugin_out = runtime.inference_adapter->TryConsumeLatestOutput(plugin_out, nullptr);
        }

        BehaviorMixResult mix_result{};
        std::vector<BehaviorMixSource> mix_sources;
        mix_sources.push_back(BehaviorMixSource{.name = "local_fsm", .output = &local_out, .global_weight = 1.0f});
        if (has_plugin_out) {
            mix_sources.push_back(BehaviorMixSource{.name = "plugin", .output = &plugin_out, .global_weight = 1.0f});
        }

        if (MixBehaviorOutputs(mix_sources, &mix_result) && bridge.build_behavior_apply_context) {
            auto apply_ctx = bridge.build_behavior_apply_context();
            ApplyBehaviorOutput(mix_result.mixed, apply_ctx);
        }
    }

    TickAppSystems(runtime, dt);
    bridge.InferTaskCategoryInplace();
}

}  // namespace k2d
