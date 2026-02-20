#include "k2d/controllers/interaction_controller.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace k2d {
namespace {

bool IsHeadPartId(const std::string &id) {
    std::string lower;
    lower.reserve(id.size());
    for (char c : id) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower.find("head") != std::string::npos;
}

void TriggerHeadPatReaction(InteractionControllerState &state) {
    state.head_pat_react_ttl = 0.35f;
}

}  // namespace

void HandleHeadPatMouseMotion(InteractionControllerState &state,
                              const InteractionControllerContext &ctx,
                              float mouse_x,
                              float mouse_y) {
    if (!ctx.model_loaded || !ctx.model || !ctx.pick_top_part_at) {
        state.head_pat_hovering = false;
        return;
    }

    const int picked = ctx.pick_top_part_at(mouse_x, mouse_y);
    bool hovering_head = false;
    if (picked >= 0 && picked < static_cast<int>(ctx.model->parts.size())) {
        const ModelPart &part = ctx.model->parts[static_cast<std::size_t>(picked)];
        hovering_head = IsHeadPartId(part.id);
    }

    if (hovering_head && !state.head_pat_hovering) {
        TriggerHeadPatReaction(state);
    }
    state.head_pat_hovering = hovering_head;
}

void UpdateHeadPatReaction(InteractionControllerState &state,
                           const InteractionControllerContext &ctx,
                           float dt_sec) {
    if (!ctx.model_loaded || !ctx.model || !ctx.has_model_params || !ctx.has_model_params()) {
        return;
    }

    state.head_pat_react_ttl = std::max(0.0f, state.head_pat_react_ttl - std::max(0.0f, dt_sec));
    if (state.head_pat_react_ttl <= 0.0f) {
        return;
    }

    const float t = state.head_pat_react_ttl / 0.35f;
    const float pulse = std::sin((1.0f - t) * 3.1415926f);

    auto set_param_target = [&](const char *pid, float target) {
        const auto it = ctx.model->param_index.find(pid);
        if (it == ctx.model->param_index.end()) return;
        const int idx = it->second;
        if (idx < 0 || idx >= static_cast<int>(ctx.model->parameters.size())) return;
        ctx.model->parameters[static_cast<std::size_t>(idx)].param.SetTarget(target);
    };

    set_param_target("BrowY", 0.35f * pulse);
    set_param_target("MouthOpen", 0.20f * pulse);
}

}  // namespace k2d
