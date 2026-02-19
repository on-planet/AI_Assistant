#pragma once

#include <SDL3/SDL.h>

#include "k2d/core/model.h"

#include <string>
#include <vector>

namespace k2d {

// 预留：感知输入（后续接 AI/外部状态）
struct PerceptionInput {
    bool has_user_focus = false;
    bool has_pointer_inside_window = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    float audio_level = 0.0f;
    float delta_time_sec = 0.0f;
};

// 预留：行为输出（后续驱动动画/窗口/参数）
struct BehaviorOutput {
    bool request_click_through = false;
    bool request_window_visible = true;
    float target_opacity = 0.5f;
    bool request_manual_param_mode = false;
    bool request_show_debug_stats = false;
    std::string requested_model_path;
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

    // default model candidates (from first to last)
    std::vector<std::string> default_model_candidates;
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

}  // namespace k2d

