#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/core/model.h"
#include "desktoper2D/lifecycle/state/task_category_types.h"

#include <string>
#include <vector>

namespace desktoper2D {

enum class PluginParamBlendMode {
    Override = 0,
    Weighted,
};

enum class BehaviorFusionMode {
    WeightedAverage = 0,
    PriorityOverride,
};

struct BehaviorFusionConfig {
    BehaviorFusionMode mode = BehaviorFusionMode::WeightedAverage;
    float local_weight = 1.0f;
    float plugin_weight = 1.0f;
    bool normalize_by_weight_sum = true;
};

struct AppRuntimeConfig {
    // window
    int window_width = 800;
    int window_height = 600;
    float window_opacity = 0.5f;
    bool click_through = false;
    bool window_visible = true;

    // startup / debug
    bool show_debug_stats = true;
    bool manual_param_mode = false;
    bool dev_hot_reload_enabled = true;
    PluginParamBlendMode plugin_param_blend_mode = PluginParamBlendMode::Override;

    // behavior fusion (local_fsm + plugin)
    BehaviorFusionConfig behavior_fusion{};

    // face->param mapping sensor fallback template
    bool face_map_sensor_fallback_enabled = true;
    float face_map_sensor_fallback_head_yaw = 0.0f;
    float face_map_sensor_fallback_head_pitch = -0.08f;
    float face_map_sensor_fallback_eye_open = 0.38f;
    float face_map_sensor_fallback_weight = 1.0f;

    // default model candidates (from first to last)
    std::vector<std::string> default_model_candidates;

    // task category
    TaskCategoryConfig task_category;
};

struct AppBootstrapResult {
    AppRuntimeConfig runtime_config;

    bool model_loaded = false;
    std::string model_load_log;
    ModelRuntime model;

    SDL_Texture *demo_texture = nullptr;
    int demo_texture_w = 0;
    int demo_texture_h = 0;
    std::string demo_texture_error;
};

AppRuntimeConfig LoadRuntimeConfig();
AppBootstrapResult BootstrapModelAndResources(SDL_Renderer *renderer);

}  // namespace desktoper2D

