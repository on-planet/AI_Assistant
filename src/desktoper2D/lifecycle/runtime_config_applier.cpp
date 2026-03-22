#include "desktoper2D/lifecycle/runtime_config_applier.h"

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void ApplyRuntimeConfig(AppRuntime &runtime, const AppRuntimeConfig &cfg) {
    runtime.window_state.click_through = cfg.click_through;
    runtime.window_state.window_visible = cfg.window_visible;
    runtime.show_debug_stats = cfg.show_debug_stats;
    runtime.manual_param_mode = cfg.manual_param_mode;
    runtime.dev_hot_reload_enabled = cfg.dev_hot_reload_enabled;
    runtime.plugin.param_blend_mode = cfg.plugin_param_blend_mode;
    runtime.plugin.behavior_fusion_config = cfg.behavior_fusion;

    runtime.face_map_sensor_fallback_enabled = cfg.face_map_sensor_fallback_enabled;
    runtime.face_map_sensor_fallback_head_yaw = cfg.face_map_sensor_fallback_head_yaw;
    runtime.face_map_sensor_fallback_head_pitch = cfg.face_map_sensor_fallback_head_pitch;
    runtime.face_map_sensor_fallback_eye_open = cfg.face_map_sensor_fallback_eye_open;
    runtime.face_map_sensor_fallback_weight = cfg.face_map_sensor_fallback_weight;

    runtime.task_decision.config = cfg.task_category;
    runtime.task_decision.schedule.force_refresh = true;
    runtime.task_decision.schedule.poll_accum_sec = runtime.task_decision.schedule.min_interval_sec;
}

}  // namespace desktoper2D
