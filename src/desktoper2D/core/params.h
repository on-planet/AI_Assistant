#pragma once

namespace desktoper2D {

struct FloatParamSpec {
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
};

float ClampParamValue(const FloatParamSpec &spec, float v);

class FloatParam {
public:
    FloatParam() = default;
    explicit FloatParam(FloatParamSpec spec);

    void SetSpec(FloatParamSpec spec);
    void Reset();

    void SetTarget(float v);
    void SetValueImmediate(float v);

    // 二阶阻尼弹簧更新：interp_speed 作为弹簧角频率(约 1/sec 量级)。
    void Update(float dt_seconds, float interp_speed);

    float value() const { return value_; }
    float target() const { return target_; }
    const FloatParamSpec &spec() const { return spec_; }

private:
    FloatParamSpec spec_{};
    float value_ = 0.0f;
    float target_ = 0.0f;
    float velocity_ = 0.0f;
};

}  // namespace desktoper2D
