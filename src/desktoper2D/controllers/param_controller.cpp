#include "desktoper2D/controllers/param_controller.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace desktoper2D {

bool HasModelParams(const ParamControllerContext &ctx) {
    return ctx.model_loaded && ctx.model && *ctx.model_loaded && !ctx.model->parameters.empty();
}

void EnsureSelectedParamIndexValid(const ParamControllerContext &ctx) {
    if (!ctx.selected_param_index) {
        return;
    }
    if (!HasModelParams(ctx)) {
        *ctx.selected_param_index = 0;
        return;
    }

    const int count = static_cast<int>(ctx.model->parameters.size());
    *ctx.selected_param_index = std::clamp(*ctx.selected_param_index, 0, count - 1);
}

void CycleSelectedParam(const ParamControllerContext &ctx, int delta) {
    if (!ctx.selected_param_index || !HasModelParams(ctx)) {
        return;
    }

    const int count = static_cast<int>(ctx.model->parameters.size());
    int next = *ctx.selected_param_index + delta;
    next %= count;
    if (next < 0) {
        next += count;
    }
    *ctx.selected_param_index = next;
}

void AdjustSelectedParam(const ParamControllerContext &ctx, float delta) {
    if (!ctx.selected_param_index || !HasModelParams(ctx)) {
        return;
    }

    EnsureSelectedParamIndexValid(ctx);
    ModelParameter &p = ctx.model->parameters[static_cast<std::size_t>(*ctx.selected_param_index)];
    p.param.SetTarget(p.param.target() + delta);
}

void ResetSelectedParam(const ParamControllerContext &ctx) {
    if (!ctx.selected_param_index || !HasModelParams(ctx)) {
        return;
    }

    EnsureSelectedParamIndexValid(ctx);
    ModelParameter &p = ctx.model->parameters[static_cast<std::size_t>(*ctx.selected_param_index)];
    p.param.SetTarget(p.param.spec().default_value);
}

void ResetAllParams(const ParamControllerContext &ctx) {
    if (!HasModelParams(ctx)) {
        return;
    }

    for (ModelParameter &p : ctx.model->parameters) {
        p.param.SetValueImmediate(p.param.spec().default_value);
    }
}

void ToggleManualParamMode(const ParamControllerContext &ctx) {
    if (!ctx.model_loaded || !ctx.manual_param_mode || !ctx.model || !*ctx.model_loaded) {
        return;
    }

    *ctx.manual_param_mode = !*ctx.manual_param_mode;
    ctx.model->animation_channels_enabled = !*ctx.manual_param_mode;

    if (*ctx.manual_param_mode) {
        for (ModelParameter &p : ctx.model->parameters) {
            p.param.SetTarget(p.param.value());
        }
    }

    SDL_Log("Manual parameter mode: %s", *ctx.manual_param_mode ? "ON" : "OFF");
}

}  // namespace desktoper2D

