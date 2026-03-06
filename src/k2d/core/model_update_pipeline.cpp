#include "model_update_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace k2d {
namespace {

static float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

static float SafeFinite(float v, float fallback = 0.0f) {
    return std::isfinite(v) ? v : fallback;
}

static float MapLinear(float v, float in_a, float in_b, float out_a, float out_b) {
    if (!std::isfinite(v) || !std::isfinite(in_a) || !std::isfinite(in_b) ||
        !std::isfinite(out_a) || !std::isfinite(out_b)) {
        return SafeFinite(out_a, 0.0f);
    }

    if (std::abs(in_b - in_a) < 1e-6f) {
        return out_a;
    }

    const float lo = std::min(in_a, in_b);
    const float hi = std::max(in_a, in_b);
    const float v_clamped = std::clamp(v, lo, hi);
    const float t = (v_clamped - in_a) / (in_b - in_a);
    return out_a + (out_b - out_a) * t;
}

static float EvalAnimationChannelProcedural(const AnimationChannel &channel, float time_sec) {
    const float phase = channel.phase + time_sec * channel.frequency * 6.283185307179586f;
    float signal = 0.0f;

    switch (channel.type) {
        case AnimationChannelType::Breath:
            signal = std::sin(phase);
            break;
        case AnimationChannelType::HeadSway:
            signal = std::sin(phase);
            break;
        case AnimationChannelType::Blink: {
            const float s = std::max(0.0f, std::sin(phase));
            signal = 1.0f - std::pow(1.0f - s, 6.0f);
            break;
        }
    }

    return channel.bias + channel.amplitude * signal;
}

static float EvalTimelineKeys(const AnimationChannel &channel, float time_sec) {
    if (channel.keyframes.empty()) {
        return 0.0f;
    }
    if (channel.keyframes.size() == 1) {
        return channel.keyframes.front().value;
    }

    const auto &kfs = channel.keyframes;
    const float start_t = kfs.front().time_sec;
    const float end_t = kfs.back().time_sec;
    const float duration = std::max(1e-6f, end_t - start_t);

    float eval_t = time_sec;
    if (channel.timeline_wrap == TimelineWrapMode::Loop) {
        const float x = std::fmod(time_sec - start_t, duration);
        eval_t = start_t + (x < 0.0f ? (x + duration) : x);
    } else if (channel.timeline_wrap == TimelineWrapMode::PingPong) {
        const float period = duration * 2.0f;
        float x = std::fmod(time_sec - start_t, period);
        if (x < 0.0f) x += period;
        eval_t = (x <= duration) ? (start_t + x) : (end_t - (x - duration));
    }

    if (eval_t <= start_t) {
        return kfs.front().value;
    }
    if (eval_t >= end_t) {
        return kfs.back().value;
    }

    for (std::size_t i = 1; i < kfs.size(); ++i) {
        const TimelineKeyframe &a = kfs[i - 1];
        const TimelineKeyframe &b = kfs[i];
        if (eval_t <= b.time_sec) {
            if (channel.timeline_interp == TimelineInterpolation::Step) {
                return a.value;
            }
            const float span = std::max(1e-6f, b.time_sec - a.time_sec);
            const float t = std::clamp((eval_t - a.time_sec) / span, 0.0f, 1.0f);
            if (channel.timeline_interp == TimelineInterpolation::Linear) {
                return a.value + (b.value - a.value) * t;
            }

            // Hermite (cubic) with per-key tangents (dv/dt)
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
            const float h10 = t3 - 2.0f * t2 + t;
            const float h01 = -2.0f * t3 + 3.0f * t2;
            const float h11 = t3 - t2;
            const float w0 = std::clamp(a.out_weight, 0.0f, 1.0f);
            const float w1 = std::clamp(b.in_weight, 0.0f, 1.0f);
            const float m0 = a.out_tangent * span * w0;
            const float m1 = b.in_tangent * span * w1;
            return h00 * a.value + h10 * m0 + h01 * b.value + h11 * m1;
        }
    }

    return kfs.back().value;
}

static float EvalAnimationChannel(const AnimationChannel &channel, float time_sec) {
    if (!channel.keyframes.empty()) {
        return EvalTimelineKeys(channel, time_sec);
    }
    return EvalAnimationChannelProcedural(channel, time_sec);
}

float ResolveParamInterpSpeed(const std::string &param_id) {
    // 分组阻尼：眼睛快、头部中、身体慢。
    // 这里返回二阶弹簧的角频率（与 FloatParam::Update 的 interp_speed 对齐）。
    std::string lower;
    lower.reserve(param_id.size());
    for (char c : param_id) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    const auto has = [&](const char *s) { return lower.find(s) != std::string::npos; };

    // 眼睛相关（眨眼/眼球）响应更快。
    if (has("eye") || has("blink") || has("pupil")) {
        return 9.0f;
    }

    // 头部相关中等速度。
    if (has("head") || has("neck") || has("yaw") || has("pitch")) {
        return 5.5f;
    }

    // 其余默认偏慢，减少全身参数抖动。
    return 3.2f;
}

}  // namespace

void ApplyAnimationChannelTargets(ModelRuntime *model, float time_sec) {
    if (!model || !model->animation_channels_enabled) return;

    std::vector<float> channel_targets(model->parameters.size(), 0.0f);
    for (std::size_t i = 0; i < model->parameters.size(); ++i) {
        channel_targets[i] = model->parameters[i].param.spec().default_value;
    }

    if (!model->animation_channels.empty()) {
        for (const AnimationChannel &ch : model->animation_channels) {
            if (!ch.enabled) continue;
            if (ch.param_index < 0 || ch.param_index >= static_cast<int>(model->parameters.size())) continue;

            const float value = EvalAnimationChannel(ch, time_sec);
            float &target = channel_targets[static_cast<std::size_t>(ch.param_index)];
            if (ch.blend == AnimationBlendMode::Add) {
                target += value * ch.weight;
            } else {
                target = target * (1.0f - ch.weight) + value * ch.weight;
            }
        }
    } else {
        for (std::size_t i = 0; i < model->parameters.size(); ++i) {
            channel_targets[i] = std::sin(time_sec * (0.7f + static_cast<float>(i) * 0.33f));
        }
    }

    for (std::size_t i = 0; i < model->parameters.size(); ++i) {
        model->parameters[i].param.SetTarget(channel_targets[i]);
    }
}

void UpdateParameterValues(ModelRuntime *model, float dt_sec) {
    if (!model) return;
    for (ModelParameter &parameter : model->parameters) {
        const float interp_speed = ResolveParamInterpSpeed(parameter.id);
        parameter.param.Update(dt_sec, interp_speed);
    }
}

bool UpdateDirtyCachesAndCheckChanged(ModelRuntime *model) {
    if (!model) return false;

    bool params_changed = false;
    if (model->cached_param_values.size() != model->parameters.size()) {
        params_changed = true;
    } else {
        for (std::size_t i = 0; i < model->parameters.size(); ++i) {
            const float v = model->parameters[i].param.value();
            if (std::abs(v - model->cached_param_values[i]) > 1e-6f) {
                params_changed = true;
                break;
            }
        }
    }

    std::vector<float> new_part_base_signature;
    new_part_base_signature.reserve(model->parts.size() * 7);
    for (const ModelPart &part : model->parts) {
        new_part_base_signature.push_back(part.base_pos_x);
        new_part_base_signature.push_back(part.base_pos_y);
        new_part_base_signature.push_back(part.base_rot_deg);
        new_part_base_signature.push_back(part.base_scale_x);
        new_part_base_signature.push_back(part.base_scale_y);
        new_part_base_signature.push_back(part.pivot_x);
        new_part_base_signature.push_back(part.base_opacity);
    }

    if (model->part_dirty_flags.size() != model->parts.size()) {
        model->part_dirty_flags.assign(model->parts.size(), 1);
    }

    bool base_changed = false;
    if (model->cached_part_base_signature.size() != new_part_base_signature.size()) {
        base_changed = true;
        std::fill(model->part_dirty_flags.begin(), model->part_dirty_flags.end(), static_cast<std::uint8_t>(1));
    } else {
        for (std::size_t i = 0; i < model->parts.size(); ++i) {
            bool part_changed = false;
            for (std::size_t k = 0; k < 7; ++k) {
                const std::size_t idx = i * 7 + k;
                if (std::abs(new_part_base_signature[idx] - model->cached_part_base_signature[idx]) > 1e-6f) {
                    part_changed = true;
                    break;
                }
            }
            if (part_changed) {
                model->part_dirty_flags[i] = 1;
                base_changed = true;
            }
        }
    }

    if (!params_changed && !base_changed) {
        return false;
    }

    if (params_changed) {
        std::fill(model->part_dirty_flags.begin(), model->part_dirty_flags.end(), static_cast<std::uint8_t>(1));
    }

    model->cached_param_values.resize(model->parameters.size());
    for (std::size_t i = 0; i < model->parameters.size(); ++i) {
        model->cached_param_values[i] = model->parameters[i].param.value();
    }
    model->cached_part_base_signature = std::move(new_part_base_signature);
    return true;
}

void ResolveLocalPartStates(ModelRuntime *model, float time_sec, float dt_sec) {
    if (!model) return;

    const float dt = std::clamp(dt_sec, 0.0f, 0.05f);

    for (ModelPart &part : model->parts) {
        float pos_x = part.base_pos_x;
        float pos_y = part.base_pos_y;
        float rot = part.base_rot_deg;
        float scale_x = part.base_scale_x;
        float scale_y = part.base_scale_y;
        float opacity = part.base_opacity;

        for (const ParamBinding &b : part.bindings) {
            if (b.param_index < 0 || b.param_index >= static_cast<int>(model->parameters.size())) continue;
            const float pv = model->parameters[static_cast<std::size_t>(b.param_index)].param.value();
            const float mapped = MapLinear(pv, b.in_min, b.in_max, b.out_min, b.out_max);

            switch (b.type) {
                case BindingType::PosX: pos_x += mapped; break;
                case BindingType::PosY: pos_y += mapped; break;
                case BindingType::RotDeg: rot += mapped; break;
                case BindingType::ScaleX: scale_x += mapped; break;
                case BindingType::ScaleY: scale_y += mapped; break;
                case BindingType::Opacity: opacity += mapped; break;
            }
        }

        part.runtime_pos_x = SafeFinite(pos_x, part.base_pos_x);
        part.runtime_pos_y = SafeFinite(pos_y, part.base_pos_y);
        part.runtime_rot_deg = SafeFinite(rot, part.base_rot_deg);
        part.runtime_scale_x = std::max(0.05f, SafeFinite(scale_x, part.base_scale_x));
        part.runtime_scale_y = std::max(0.05f, SafeFinite(scale_y, part.base_scale_y));
        part.runtime_opacity = Clamp01(SafeFinite(opacity, part.base_opacity));

        if (part.deformer_type == DeformerType::Warp) {
            part.ffd.weight.Update(dt_sec, 2.0f);
            for (int y = 0; y < part.ffd.control_rows; ++y) {
                for (int x = 0; x < part.ffd.control_cols; ++x) {
                    const float nx = (part.ffd.control_cols <= 1) ? 0.0f
                        : (static_cast<float>(x) / static_cast<float>(part.ffd.control_cols - 1)) * 2.0f - 1.0f;
                    const float ny = (part.ffd.control_rows <= 1) ? 0.0f
                        : (static_cast<float>(y) / static_cast<float>(part.ffd.control_rows - 1)) * 2.0f - 1.0f;
                    const float r2 = nx * nx + ny * ny;
                    const float falloff = std::exp(-r2 * 2.0f);
                    const int idx = y * part.ffd.control_cols + x;
                    part.ffd.offsets[static_cast<std::size_t>(idx)].dx =
                        std::sin(time_sec * 1.7f + nx * 1.2f + ny * 2.1f) * 8.0f * falloff;
                    part.ffd.offsets[static_cast<std::size_t>(idx)].dy =
                        std::cos(time_sec * 1.2f + nx * 2.2f - ny * 1.3f) * 6.0f * falloff;
                }
            }
            part.rotation_deformer_deg = 0.0f;
        } else {
            part.ffd.weight.SetValueImmediate(0.0f);
            const float amp_deg = 12.0f * std::clamp(part.rotation_deformer_weight, 0.0f, 1.0f);
            part.rotation_deformer_deg = std::sin(time_sec * std::max(0.0f, part.rotation_deformer_speed)) * amp_deg;
        }
    }

    if (model->spring_offset_x.size() != model->parts.size()) {
        model->spring_offset_x.assign(model->parts.size(), 0.0f);
        model->spring_offset_y.assign(model->parts.size(), 0.0f);
        model->spring_vel_x.assign(model->parts.size(), 0.0f);
        model->spring_vel_y.assign(model->parts.size(), 0.0f);
        model->spring_initialized = false;
    }

    if (!model->enable_hair_spring) {
        std::fill(model->spring_offset_x.begin(), model->spring_offset_x.end(), 0.0f);
        std::fill(model->spring_offset_y.begin(), model->spring_offset_y.end(), 0.0f);
        std::fill(model->spring_vel_x.begin(), model->spring_vel_x.end(), 0.0f);
        std::fill(model->spring_vel_y.begin(), model->spring_vel_y.end(), 0.0f);
        model->spring_initialized = false;
        return;
    }

    auto head_it = model->part_index.find("HeadBase");
    if (head_it == model->part_index.end()) {
        return;
    }
    const int head_idx = head_it->second;
    if (head_idx < 0 || head_idx >= static_cast<int>(model->parts.size())) {
        return;
    }

    const float head_x = model->parts[static_cast<std::size_t>(head_idx)].runtime_pos_x;
    const float head_y = model->parts[static_cast<std::size_t>(head_idx)].runtime_pos_y;

    if (!model->spring_initialized) {
        model->spring_head_prev_x = head_x;
        model->spring_head_prev_y = head_y;
        model->spring_initialized = true;
        return;
    }

    const float head_dx = head_x - model->spring_head_prev_x;
    const float head_dy = head_y - model->spring_head_prev_y;
    model->spring_head_prev_x = head_x;
    model->spring_head_prev_y = head_y;

    auto has_tag = [](const std::string &id, const char *tag) {
        return id.find(tag) != std::string::npos;
    };

    for (std::size_t i = 0; i < model->parts.size(); ++i) {
        ModelPart &part = model->parts[i];
        const bool spring_target =
            part.parent_index == head_idx ||
            has_tag(part.id, "Hair") || has_tag(part.id, "hair") ||
            has_tag(part.id, "Bang") || has_tag(part.id, "bang");
        if (!spring_target) {
            continue;
        }

        const float target_x = -head_dx * 0.35f;
        const float target_y = -head_dy * 0.35f;
        const float stiffness = 18.0f;
        const float damping = 6.5f;

        float &ox = model->spring_offset_x[i];
        float &oy = model->spring_offset_y[i];
        float &vx = model->spring_vel_x[i];
        float &vy = model->spring_vel_y[i];

        vx += (stiffness * (target_x - ox) - damping * vx) * dt;
        vy += (stiffness * (target_y - oy) - damping * vy) * dt;
        ox += vx * dt;
        oy += vy * dt;

        part.runtime_pos_x += ox;
        part.runtime_pos_y += oy;
    }
}

void ResolveWorldTransforms(ModelRuntime *model) {
    if (!model) return;

    const std::size_t n = model->parts.size();
    std::vector<float> world_x(n, 0.0f);
    std::vector<float> world_y(n, 0.0f);
    std::vector<float> world_rot(n, 0.0f);
    std::vector<float> world_sx(n, 1.0f);
    std::vector<float> world_sy(n, 1.0f);
    std::vector<float> world_opacity(n, 1.0f);
    std::vector<uint8_t> visit(n, 0);

    auto deg_to_rad = [](float deg) {
        return deg * 3.14159265358979323846f / 180.0f;
    };

    std::function<void(int)> resolve_world = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(n)) return;
        if (visit[static_cast<std::size_t>(idx)] == 2) return;
        if (visit[static_cast<std::size_t>(idx)] == 1) {
            model->parts[static_cast<std::size_t>(idx)].parent_index = -1;
            visit[static_cast<std::size_t>(idx)] = 2;
            world_x[static_cast<std::size_t>(idx)] = model->parts[static_cast<std::size_t>(idx)].runtime_pos_x;
            world_y[static_cast<std::size_t>(idx)] = model->parts[static_cast<std::size_t>(idx)].runtime_pos_y;
            world_rot[static_cast<std::size_t>(idx)] = model->parts[static_cast<std::size_t>(idx)].runtime_rot_deg;
            world_sx[static_cast<std::size_t>(idx)] = model->parts[static_cast<std::size_t>(idx)].runtime_scale_x;
            world_sy[static_cast<std::size_t>(idx)] = model->parts[static_cast<std::size_t>(idx)].runtime_scale_y;
            world_opacity[static_cast<std::size_t>(idx)] = model->parts[static_cast<std::size_t>(idx)].runtime_opacity;
            return;
        }

        visit[static_cast<std::size_t>(idx)] = 1;
        ModelPart &part = model->parts[static_cast<std::size_t>(idx)];

        const int pidx = part.parent_index;
        if (pidx >= 0 && pidx < static_cast<int>(n)) {
            resolve_world(pidx);

            const float pr = deg_to_rad(world_rot[static_cast<std::size_t>(pidx)]);
            const float c = std::cos(pr);
            const float s = std::sin(pr);

            const float lx = part.runtime_pos_x * world_sx[static_cast<std::size_t>(pidx)];
            const float ly = part.runtime_pos_y * world_sy[static_cast<std::size_t>(pidx)];

            const float rx = lx * c - ly * s;
            const float ry = lx * s + ly * c;

            world_x[static_cast<std::size_t>(idx)] = world_x[static_cast<std::size_t>(pidx)] + rx;
            world_y[static_cast<std::size_t>(idx)] = world_y[static_cast<std::size_t>(pidx)] + ry;
            world_rot[static_cast<std::size_t>(idx)] = world_rot[static_cast<std::size_t>(pidx)] + part.runtime_rot_deg;
            world_sx[static_cast<std::size_t>(idx)] = world_sx[static_cast<std::size_t>(pidx)] * part.runtime_scale_x;
            world_sy[static_cast<std::size_t>(idx)] = world_sy[static_cast<std::size_t>(pidx)] * part.runtime_scale_y;
            world_opacity[static_cast<std::size_t>(idx)] = world_opacity[static_cast<std::size_t>(pidx)] * part.runtime_opacity;
        } else {
            world_x[static_cast<std::size_t>(idx)] = part.runtime_pos_x;
            world_y[static_cast<std::size_t>(idx)] = part.runtime_pos_y;
            world_rot[static_cast<std::size_t>(idx)] = part.runtime_rot_deg;
            world_sx[static_cast<std::size_t>(idx)] = part.runtime_scale_x;
            world_sy[static_cast<std::size_t>(idx)] = part.runtime_scale_y;
            world_opacity[static_cast<std::size_t>(idx)] = part.runtime_opacity;
        }

        visit[static_cast<std::size_t>(idx)] = 2;
    };

    for (int i = 0; i < static_cast<int>(n); ++i) {
        resolve_world(i);
    }

    for (std::size_t i = 0; i < n; ++i) {
        ModelPart &part = model->parts[i];
        part.runtime_pos_x = SafeFinite(world_x[i], part.runtime_pos_x);
        part.runtime_pos_y = SafeFinite(world_y[i], part.runtime_pos_y);
        part.runtime_rot_deg = SafeFinite(world_rot[i], part.runtime_rot_deg);
        part.runtime_scale_x = std::max(0.05f, SafeFinite(world_sx[i], part.runtime_scale_x));
        part.runtime_scale_y = std::max(0.05f, SafeFinite(world_sy[i], part.runtime_scale_y));
        part.runtime_opacity = Clamp01(SafeFinite(world_opacity[i], part.runtime_opacity));
    }
}

void ApplyWorldDeforms(ModelRuntime *model, float dt_sec) {
    if (!model) return;
    for (std::size_t i = 0; i < model->parts.size(); ++i) {
        ModelPart &part = model->parts[i];
        const bool part_dirty = i < model->part_dirty_flags.size() && model->part_dirty_flags[i] != 0;
        if (!part_dirty && !part.deformer_dirty && !part.transform_dirty) {
            continue;
        }

        part.deform.translate_x.SetTarget(part.runtime_pos_x);
        part.deform.translate_y.SetTarget(part.runtime_pos_y);
        part.deform.rotation_deg.SetTarget(part.runtime_rot_deg);
        part.deform.scale_x.SetTarget(part.runtime_scale_x);
        part.deform.scale_y.SetTarget(part.runtime_scale_y);
        part.deform.Update(dt_sec);

        // 1) 先只重算仿射结果（每帧必要）。
        ApplyAffineDeform(part.mesh, &part.deformed_positions, part.deform);

        // 2) 局部更新 FFD：仅在控制点偏移/权重变化时，重建每顶点 FFD 增量缓存。
        bool rebuild_ffd_delta = false;
        if (part.ffd_prev_offsets.size() != part.ffd.offsets.size() ||
            part.ffd_vertex_dxdy.size() != part.mesh.positions.size()) {
            rebuild_ffd_delta = true;
        } else {
            for (std::size_t i = 0; i < part.ffd.offsets.size(); ++i) {
                const auto &prev = part.ffd_prev_offsets[i];
                const auto &curr = part.ffd.offsets[i];
                if (std::abs(prev.dx - curr.dx) > 1e-6f || std::abs(prev.dy - curr.dy) > 1e-6f) {
                    rebuild_ffd_delta = true;
                    break;
                }
            }
        }

        if (rebuild_ffd_delta) {
            part.ffd_vertex_dxdy.assign(part.mesh.positions.size(), 0.0f);

            const int cols = part.ffd.control_cols;
            const int rows = part.ffd.control_rows;
            const bool valid_lattice = cols >= 2 && rows >= 2 &&
                part.ffd.offsets.size() == static_cast<std::size_t>(cols * rows) &&
                part.mesh.uvs.size() == part.mesh.positions.size();

            if (valid_lattice) {
                auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
                auto sample_bilinear = [&](float u, float v, bool sample_x) {
                    const float fx = clamp01(u) * static_cast<float>(cols - 1);
                    const float fy = clamp01(v) * static_cast<float>(rows - 1);
                    const int x0 = static_cast<int>(std::floor(fx));
                    const int y0 = static_cast<int>(std::floor(fy));
                    const int x1 = std::min(x0 + 1, cols - 1);
                    const int y1 = std::min(y0 + 1, rows - 1);
                    const float tx = fx - static_cast<float>(x0);
                    const float ty = fy - static_cast<float>(y0);

                    auto get = [&](int x, int y) -> float {
                        const auto &cp = part.ffd.offsets[static_cast<std::size_t>(y * cols + x)];
                        return sample_x ? cp.dx : cp.dy;
                    };

                    const float p00 = get(x0, y0);
                    const float p10 = get(x1, y0);
                    const float p01 = get(x0, y1);
                    const float p11 = get(x1, y1);
                    const float a = p00 + (p10 - p00) * tx;
                    const float b = p01 + (p11 - p01) * tx;
                    return a + (b - a) * ty;
                };

                for (std::size_t i = 0; i + 1 < part.mesh.uvs.size(); i += 2) {
                    const float u = part.mesh.uvs[i];
                    const float v = part.mesh.uvs[i + 1];
                    part.ffd_vertex_dxdy[i] = sample_bilinear(u, v, true);
                    part.ffd_vertex_dxdy[i + 1] = sample_bilinear(u, v, false);
                }
            }

            part.ffd_prev_offsets = part.ffd.offsets;
        }

        // 3) Deformer 分层执行：Warp 或 Rotation。
        if (part.deformer_type == DeformerType::Warp) {
            const float w = std::clamp(part.ffd.weight.value(), 0.0f, 1.0f);
            if (w > 0.0f && part.ffd_vertex_dxdy.size() == part.deformed_positions.size()) {
                for (std::size_t i = 0; i + 1 < part.deformed_positions.size(); i += 2) {
                    part.deformed_positions[i] += part.ffd_vertex_dxdy[i] * w;
                    part.deformed_positions[i + 1] += part.ffd_vertex_dxdy[i + 1] * w;
                }
            }
            part.ffd_prev_weight = w;
        } else {
            ApplyRotationDeltaDeform(&part.deformed_positions, part.rotation_deformer_deg);
            part.ffd_prev_weight = 0.0f;
        }
        part.transform_dirty = false;
        part.deformer_dirty = false;
        part.ffd_delta_dirty = false;
        part.render_cache_dirty = true;
    }

    std::fill(model->part_dirty_flags.begin(), model->part_dirty_flags.end(), static_cast<std::uint8_t>(0));
}

}  // namespace k2d
