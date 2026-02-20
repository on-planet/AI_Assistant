#pragma once

#include "k2d/core/model.h"

#include <functional>
#include <string>

namespace k2d {

struct InteractionControllerState {
    float head_pat_react_ttl = 0.0f;
    bool head_pat_hovering = false;
};

struct InteractionControllerContext {
    bool model_loaded = false;
    ModelRuntime *model = nullptr;
    std::function<int(float, float)> pick_top_part_at;
    std::function<bool()> has_model_params;
};

void HandleHeadPatMouseMotion(InteractionControllerState &state,
                              const InteractionControllerContext &ctx,
                              float mouse_x,
                              float mouse_y);

void UpdateHeadPatReaction(InteractionControllerState &state,
                           const InteractionControllerContext &ctx,
                           float dt_sec);

}  // namespace k2d
