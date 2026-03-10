#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/controllers/app_bootstrap.h"
#include "desktoper2D/core/model.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace desktoper2D {

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

struct BehaviorMixSource {
    const char *name = "unknown";
    const BehaviorOutput *output = nullptr;
    float global_weight = 1.0f;
};

struct BehaviorMixResult {
    BehaviorOutput mixed;
    std::unordered_map<std::string, std::string> dominant_source_by_param;
};

bool MixBehaviorOutputs(const std::vector<BehaviorMixSource> &sources,
                        const BehaviorFusionConfig &fusion_cfg,
                        BehaviorMixResult *out_result);

void ApplyBehaviorOutput(const BehaviorOutput &out, const BehaviorApplyContext &ctx);

}  // namespace desktoper2D
