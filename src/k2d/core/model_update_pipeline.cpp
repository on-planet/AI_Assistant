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

static float EvalAnimationChannel(const AnimationChannel &channel, float time_sec) {
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
        parameter.param.Update(dt_sec, 4.0f);
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

    bool base_changed = false;
    if (model->cached_part_base_signature.size() != new_part_base_signature.size()) {
        base_changed = true;
    } else {
        for (std::size_t i = 0; i < new_part_base_signature.size(); ++i) {
            if (std::abs(new_part_base_signature[i] - model->cached_part_base_signature[i]) > 1e-6f) {
                base_changed = true;
                break;
            }
        }
    }

    if (!params_changed && !base_changed) {
        return false;
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

        part.ffd.weight.SetTarget(0.45f);
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
    for (ModelPart &part : model->parts) {
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

        // 3) 每帧仅按当前权重混合已缓存的 FFD 增量（避免全量重采样）。
        const float w = std::clamp(part.ffd.weight.value(), 0.0f, 1.0f);
        if (w > 0.0f && part.ffd_vertex_dxdy.size() == part.deformed_positions.size()) {
            for (std::size_t i = 0; i + 1 < part.deformed_positions.size(); i += 2) {
                part.deformed_positions[i] += part.ffd_vertex_dxdy[i] * w;
                part.deformed_positions[i + 1] += part.ffd_vertex_dxdy[i + 1] * w;
            }
        }
        part.ffd_prev_weight = w;
    }
}

}  // namespace k2d
