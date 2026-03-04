#include "k2d/lifecycle/systems/runtime_tick_entry.h"

#include <vector>

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
            in.vision.head_yaw_deg = 0.0f;
            in.vision.head_pitch_deg = 0.0f;
            in.vision.head_roll_deg = 0.0f;
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
