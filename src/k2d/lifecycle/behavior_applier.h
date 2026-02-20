#pragma once

#include <SDL3/SDL.h>

#include "k2d/controllers/app_bootstrap.h"
#include "k2d/core/model.h"
#include "k2d/lifecycle/plugin_lifecycle.h"

namespace k2d {

struct BehaviorApplyContext {
    bool model_loaded = false;
    ModelRuntime *model = nullptr;

    PluginParamBlendMode plugin_param_blend_mode = PluginParamBlendMode::Override;

    SDL_Window *window = nullptr;
    bool *show_debug_stats = nullptr;
    bool *manual_param_mode = nullptr;

    void (*set_click_through)(bool enabled, void *user_data) = nullptr;
    void *set_click_through_user_data = nullptr;

    void (*sync_animation_channel_state)(void *user_data) = nullptr;
    void *sync_animation_channel_state_user_data = nullptr;
};

void ApplyBehaviorOutput(const BehaviorOutput &out, const BehaviorApplyContext &ctx);

}  // namespace k2d
