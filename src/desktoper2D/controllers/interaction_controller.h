#pragma once

#include "desktoper2D/core/model.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <functional>
#include <string>

namespace desktoper2D {

enum class BaseEmotionState {
    Idle,
    Focus,
    Sleepy,
};

enum class OverlayEmotionState {
    None,
    Blink,
    HeadPat,
    Surprised,
};

struct InteractionControllerState {
    // 层级状态机
    BaseEmotionState base_state = BaseEmotionState::Idle;
    OverlayEmotionState overlay_state = OverlayEmotionState::None;

    // 高优先级交互：摸头反应。
    float head_pat_react_ttl = 0.0f;
    bool head_pat_hovering = false;

    // 基础状态机内部状态
    float fsm_time_sec = 0.0f;
    float base_state_remain_sec = 2.5f;

    // Blink overlay
    float blink_interval_remain_sec = 2.2f;
    float blink_anim_remain_sec = 0.0f;

    // Focus/Idle 注视目标
    float look_target_x = 0.0f;
    float look_target_y = 0.0f;
    float look_hold_remain_sec = 0.8f;

    // Head 轻摆
    float head_sway_phase = 0.0f;

    // Surprised overlay（预留触发）
    float surprised_react_ttl = 0.0f;
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

void HandleHeadPatMouseDown(InteractionControllerState &state,
                            const InteractionControllerContext &ctx,
                            float mouse_x,
                            float mouse_y);

void UpdateHeadPatReaction(InteractionControllerState &state,
                           const InteractionControllerContext &ctx,
                           float dt_sec);

void BuildInteractionBehaviorOutput(InteractionControllerState &state,
                                    const InteractionControllerContext &ctx,
                                    float dt_sec,
                                    BehaviorOutput &out);

}  // namespace desktoper2D
