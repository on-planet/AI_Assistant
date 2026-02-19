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
}

void FloatParam::SetTarget(float v) {
    target_ = ClampParamValue(spec_, v);
}

void FloatParam::SetValueImmediate(float v) {
    const float clamped = ClampParamValue(spec_, v);
    value_ = clamped;
    target_ = clamped;
}

void FloatParam::Update(float dt_seconds, float interp_speed) {
    if (dt_seconds <= 0.0f) {
        return;
    }

    if (interp_speed <= 0.0f) {
        value_ = target_;
        return;
    }

    const float alpha = 1.0f - std::exp(-interp_speed * dt_seconds);
    value_ += (target_ - value_) * alpha;
    value_ = ClampParamValue(spec_, value_);
}

}  // namespace k2d
