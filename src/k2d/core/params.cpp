#include "params.h"

#include <algorithm>
#include <cmath>

namespace k2d {

float ClampParamValue(const FloatParamSpec &spec, float v) {
    const float lo = std::min(spec.min_value, spec.max_value);
    const float hi = std::max(spec.min_value, spec.max_value);
    return std::clamp(v, lo, hi);
}

FloatParam::FloatParam(FloatParamSpec spec) {
    SetSpec(spec);
}

void FloatParam::SetSpec(FloatParamSpec spec) {
    spec_ = spec;
    spec_.default_value = ClampParamValue(spec_, spec_.default_value);
    value_ = ClampParamValue(spec_, value_);
    target_ = ClampParamValue(spec_, target_);
}

void FloatParam::Reset() {
    value_ = spec_.default_value;
    target_ = spec_.default_value;
    velocity_ = 0.0f;
}

void FloatParam::SetTarget(float v) {
    target_ = ClampParamValue(spec_, v);
}

void FloatParam::SetValueImmediate(float v) {
    const float clamped = ClampParamValue(spec_, v);
    value_ = clamped;
    target_ = clamped;
    velocity_ = 0.0f;
}

void FloatParam::Update(float dt_seconds, float interp_speed) {
    if (dt_seconds <= 0.0f) {
        return;
    }

    if (interp_speed <= 0.0f) {
        value_ = target_;
        velocity_ = 0.0f;
        return;
    }

    // 二阶阻尼弹簧（半隐式欧拉）：
    // x'' + 2*zeta*omega*x' + omega^2*(x-target)=0
    // 取接近临界阻尼，减少过冲并保留“弹性跟随”手感。
    const float dt = std::min(dt_seconds, 1.0f / 30.0f);
    const float omega = std::max(0.1f, interp_speed);
    constexpr float zeta = 0.92f;

    const float displacement = value_ - target_;
    const float accel = -2.0f * zeta * omega * velocity_ - omega * omega * displacement;
    velocity_ += accel * dt;
    value_ += velocity_ * dt;

    // 数值保护：非常接近目标时直接收敛，避免尾部抖动。
    if (std::abs(target_ - value_) < 1e-5f && std::abs(velocity_) < 1e-5f) {
        value_ = target_;
        velocity_ = 0.0f;
    }

    value_ = ClampParamValue(spec_, value_);
}

}  // namespace k2d
