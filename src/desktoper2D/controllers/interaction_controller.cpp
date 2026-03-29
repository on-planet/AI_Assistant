#include "desktoper2D/controllers/interaction_controller.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>

namespace desktoper2D {
namespace {

bool IsHeadPartId(const std::string &id) {
    std::string lower;
    lower.reserve(id.size());
    for (char c : id) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower.find("head") != std::string::npos;
}

bool IsHeadPartOrAncestor(const ModelRuntime &model, int part_index) {
    const int max_steps = static_cast<int>(model.parts.size());
    int idx = part_index;
    for (int step = 0; step < max_steps; ++step) {
        if (idx < 0 || idx >= static_cast<int>(model.parts.size())) {
            break;
        }
        const ModelPart &part = model.parts[static_cast<std::size_t>(idx)];
        if (IsHeadPartId(part.id)) {
            return true;
        }
        idx = part.parent_index;
    }
    return false;
}

void TriggerHeadPatReaction(InteractionControllerState &state) {
    state.head_pat_react_ttl = 0.35f;
}

float TrianglePulse01(float t01) {
    const float t = std::clamp(t01, 0.0f, 1.0f);
    return 1.0f - std::abs(2.0f * t - 1.0f);
}

void ComputeInteractionBehaviorOutput(InteractionControllerState &state,
                                      const InteractionControllerContext &ctx,
                                      float dt_sec,
                                      BehaviorOutput &out) {
    out.ClearPreservingCapacity();

    const float dt = std::max(0.0f, dt_sec);
    state.fsm_time_sec += dt;

    auto set_out_target = [&](const char *pid, float target, float weight = 1.0f) {
        out.param_targets[pid] = target;
        out.param_weights[pid] = std::clamp(weight, 0.0f, 1.0f);
    };

    // --- Base FSM: Idle / Focus / Sleepy ---
    state.base_state_remain_sec -= dt;
    if (state.base_state_remain_sec <= 0.0f) {
        const int cycle = static_cast<int>(state.fsm_time_sec) % 3;
        state.base_state = (cycle == 0) ? BaseEmotionState::Idle
                         : (cycle == 1) ? BaseEmotionState::Focus
                                        : BaseEmotionState::Sleepy;
        state.base_state_remain_sec = (state.base_state == BaseEmotionState::Focus) ? 2.2f
                              : (state.base_state == BaseEmotionState::Sleepy) ? 3.0f
                                                                               : 2.6f;
    }

    const float breath = 0.5f + 0.5f * std::sin(state.fsm_time_sec * 1.2f);
    state.head_sway_phase += dt * 0.9f;
    const float head_sway = std::sin(state.head_sway_phase);

    if (state.base_state == BaseEmotionState::Sleepy) {
        state.look_target_x *= std::exp(-dt * 4.0f);
        state.look_target_y *= std::exp(-dt * 4.0f);
    } else {
        state.look_hold_remain_sec -= dt;
        if (state.look_hold_remain_sec <= 0.0f) {
            const std::int32_t tick = static_cast<std::int32_t>(state.fsm_time_sec * 10.0f);
            const float nx = static_cast<float>((tick % 7) - 3) / 3.0f;
            const float ny = static_cast<float>(((tick / 3) % 7) - 3) / 3.0f;
            const float scale = (state.base_state == BaseEmotionState::Focus) ? 0.95f : 0.65f;
            state.look_target_x = std::clamp(nx * scale, -1.0f, 1.0f);
            state.look_target_y = std::clamp(ny * (scale * 0.7f), -1.0f, 1.0f);
            state.look_hold_remain_sec = (state.base_state == BaseEmotionState::Focus) ? 0.6f : 1.0f;
        }
    }

    // --- Overlay FSM: Blink / HeadPat / Surprised ---
    state.head_pat_react_ttl = std::max(0.0f, state.head_pat_react_ttl - dt);
    state.surprised_react_ttl = std::max(0.0f, state.surprised_react_ttl - dt);

    state.blink_interval_remain_sec -= dt;
    if (state.blink_interval_remain_sec <= 0.0f && state.blink_anim_remain_sec <= 0.0f) {
        state.blink_anim_remain_sec = (state.base_state == BaseEmotionState::Sleepy) ? 0.16f : 0.12f;
        state.blink_interval_remain_sec = (state.base_state == BaseEmotionState::Focus) ? 2.4f : 1.9f;
    }

    float blink_close = 0.0f;
    if (state.blink_anim_remain_sec > 0.0f) {
        const float blink_dur = (state.base_state == BaseEmotionState::Sleepy) ? 0.16f : 0.12f;
        state.blink_anim_remain_sec = std::max(0.0f, state.blink_anim_remain_sec - dt);
        const float t01 = 1.0f - (state.blink_anim_remain_sec / blink_dur);
        blink_close = TrianglePulse01(t01);
    }

    if (state.head_pat_react_ttl > 0.0f) {
        state.overlay_state = OverlayEmotionState::HeadPat;
    } else if (state.surprised_react_ttl > 0.0f) {
        state.overlay_state = OverlayEmotionState::Surprised;
    } else if (blink_close > 0.001f) {
        state.overlay_state = OverlayEmotionState::Blink;
    } else {
        state.overlay_state = OverlayEmotionState::None;
    }

    const float pat_weight = (state.head_pat_react_ttl > 0.0f)
                           ? std::sin((1.0f - state.head_pat_react_ttl / 0.35f) * 3.1415926f)
                           : 0.0f;

    float brow = 0.0f;
    float mouth = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float eye_x = 0.0f;
    float eye_y = 0.0f;
    float eye_open = 1.0f - blink_close;

    switch (state.base_state) {
        case BaseEmotionState::Idle:
            brow = 0.08f * breath;
            mouth = 0.05f + 0.08f * breath;
            yaw = 0.16f * head_sway + 0.20f * state.look_target_x;
            pitch = 0.10f * std::sin(state.head_sway_phase * 0.7f) + 0.14f * state.look_target_y;
            eye_x = 0.50f * state.look_target_x;
            eye_y = 0.40f * state.look_target_y;
            break;
        case BaseEmotionState::Focus:
            brow = 0.04f * breath;
            mouth = 0.03f + 0.05f * breath;
            yaw = 0.10f * head_sway + 0.28f * state.look_target_x;
            pitch = 0.06f * std::sin(state.head_sway_phase * 0.7f) + 0.18f * state.look_target_y;
            eye_x = 0.65f * state.look_target_x;
            eye_y = 0.52f * state.look_target_y;
            break;
        case BaseEmotionState::Sleepy:
            brow = -0.05f + 0.03f * breath;
            mouth = 0.01f + 0.03f * breath;
            yaw = 0.07f * head_sway;
            pitch = -0.03f + 0.05f * std::sin(state.head_sway_phase * 0.7f);
            eye_x = 0.20f * state.look_target_x;
            eye_y = 0.18f * state.look_target_y;
            eye_open = std::min(eye_open, 0.72f);
            break;
    }

    if (state.overlay_state == OverlayEmotionState::HeadPat) {
        brow += 0.30f * pat_weight;
        mouth += 0.18f * pat_weight;
    } else if (state.overlay_state == OverlayEmotionState::Surprised) {
        const float w = std::sin((1.0f - state.surprised_react_ttl / 0.25f) * 3.1415926f);
        brow += 0.22f * w;
        mouth += 0.30f * w;
        eye_open = std::max(eye_open, 0.95f);
    }

    set_out_target("BrowY", brow);
    set_out_target("MouthOpen", mouth);
    set_out_target("HeadYaw", yaw);
    set_out_target("HeadPitch", pitch);
    set_out_target("EyeBallX", eye_x);
    set_out_target("EyeBallY", eye_y);

    const float eye_open_clamped = std::clamp(eye_open, 0.0f, 1.0f);
    set_out_target("EyeOpen", eye_open_clamped);
    set_out_target("EyeLOpen", eye_open_clamped);
    set_out_target("EyeROpen", eye_open_clamped);
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
    state.head_pat_hovering = IsHeadPartOrAncestor(*ctx.model, picked);
}

void HandleHeadPatMouseDown(InteractionControllerState &state,
                            const InteractionControllerContext &ctx,
                            float mouse_x,
                            float mouse_y) {
    if (!ctx.model_loaded || !ctx.model || !ctx.pick_top_part_at) {
        return;
    }

    const int picked = ctx.pick_top_part_at(mouse_x, mouse_y);
    if (picked < 0 || picked >= static_cast<int>(ctx.model->parts.size())) {
        return;
    }

    // 点击任意命中的模型部件都触发反应，避免仅靠 head 命名约束导致点击无效。
    TriggerHeadPatReaction(state);
    state.head_pat_hovering = IsHeadPartOrAncestor(*ctx.model, picked);
}

void UpdateHeadPatReaction(InteractionControllerState &state,
                           const InteractionControllerContext &ctx,
                           float dt_sec) {
    if (!ctx.model_loaded || !ctx.model || !ctx.has_model_params || !ctx.has_model_params()) {
        return;
    }

    BehaviorOutput out{};
    ComputeInteractionBehaviorOutput(state, ctx, dt_sec, out);

    for (const auto &kv : out.param_targets) {
        const auto it = ctx.model->param_index.find(kv.first);
        if (it == ctx.model->param_index.end()) continue;
        const int idx = it->second;
        if (idx < 0 || idx >= static_cast<int>(ctx.model->parameters.size())) continue;
        ctx.model->parameters[static_cast<std::size_t>(idx)].param.SetTarget(kv.second);
    }
}

void BuildInteractionBehaviorOutput(InteractionControllerState &state,
                                    const InteractionControllerContext &ctx,
                                    float dt_sec,
                                    BehaviorOutput &out) {
    if (!ctx.model_loaded || !ctx.model || !ctx.has_model_params || !ctx.has_model_params()) {
        out.ClearPreservingCapacity();
        return;
    }
    ComputeInteractionBehaviorOutput(state, ctx, dt_sec, out);
}

}  // namespace desktoper2D
