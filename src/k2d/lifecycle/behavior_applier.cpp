#include "k2d/lifecycle/behavior_applier.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace k2d {
namespace {

bool IsFinite(float v) {
    return std::isfinite(v);
}

float ResolveWeight(const std::unordered_map<std::string, float> &weights,
                    const std::string &key,
                    bool *ok,
                    bool *clamped,
                    bool *non_positive) {
    *ok = false;
    *clamped = false;
    *non_positive = false;

    const auto it = weights.find(key);
    if (it == weights.end()) {
        return 0.0f;
    }
    if (!IsFinite(it->second)) {
        return 0.0f;
    }

    *ok = true;
    const float w = std::clamp(it->second, 0.0f, 1.0f);
    *clamped = (w != it->second);
    *non_positive = (w <= 0.0f);
    return w;
}

}  // namespace

bool MixBehaviorOutputs(const std::vector<BehaviorMixSource> &sources, BehaviorMixResult *out_result) {
    if (!out_result) return false;

    out_result->mixed = BehaviorOutput{};
    out_result->dominant_source_by_param.clear();

    std::unordered_map<std::string, float> sum_weight_by_param;
    std::unordered_map<std::string, float> sum_weighted_target_by_param;
    std::unordered_map<std::string, float> dominant_weight_by_param;

    for (const auto &src : sources) {
        if (!src.output || src.global_weight <= 0.0f) continue;

        for (const auto &kv : src.output->param_targets) {
            bool has_weight = false;
            bool weight_clamped = false;
            bool weight_non_positive = false;
            const float local_w = ResolveWeight(src.output->param_weights,
                                                kv.first,
                                                &has_weight,
                                                &weight_clamped,
                                                &weight_non_positive);
            (void)weight_clamped;
            if (!has_weight || weight_non_positive) continue;
            if (!IsFinite(kv.second)) continue;

            const float effective_w = std::clamp(local_w * src.global_weight, 0.0f, 1.0f);
            if (effective_w <= 0.0f) continue;

            sum_weight_by_param[kv.first] += effective_w;
            sum_weighted_target_by_param[kv.first] += kv.second * effective_w;

            auto it = dominant_weight_by_param.find(kv.first);
            if (it == dominant_weight_by_param.end() || effective_w > it->second) {
                dominant_weight_by_param[kv.first] = effective_w;
                out_result->dominant_source_by_param[kv.first] = (src.name ? std::string(src.name) : std::string("unknown"));
            }
        }

        out_result->mixed.trigger_blink = out_result->mixed.trigger_blink || src.output->trigger_blink;
        out_result->mixed.trigger_idle_shift = out_result->mixed.trigger_idle_shift || src.output->trigger_idle_shift;
    }

    for (const auto &kv : sum_weight_by_param) {
        const std::string &pid = kv.first;
        const float w_sum = kv.second;
        if (w_sum <= 1e-6f) continue;

        out_result->mixed.param_targets[pid] = sum_weighted_target_by_param[pid] / w_sum;
        out_result->mixed.param_weights[pid] = std::clamp(w_sum, 0.0f, 1.0f);
    }

    return !out_result->mixed.param_targets.empty();
}

void ApplyBehaviorOutput(const BehaviorOutput &out, const BehaviorApplyContext &ctx) {
    static std::unordered_map<std::string, Uint64> s_last_log_ms;
    const Uint64 now_ms = SDL_GetTicks();
    auto should_log = [&](const std::string &key) {
        Uint64 &last = s_last_log_ms[key];
        if (now_ms - last >= 2000) {
            last = now_ms;
            return true;
        }
        return false;
    };

    if (ctx.model_loaded && ctx.model && !ctx.model->parameters.empty()) {
        for (const auto &kv : out.param_targets) {
            bool has_weight = false;
            bool weight_clamped = false;
            bool weight_non_positive = false;
            const float w = ResolveWeight(out.param_weights, kv.first, &has_weight, &weight_clamped, &weight_non_positive);
            if (!has_weight) {
                if (out.param_weights.find(kv.first) != out.param_weights.end() &&
                    should_log("plugin.invalid.weight.nan." + kv.first)) {
                    SDL_Log("Plugin param ignored: id='%s' weight is NaN/Inf", kv.first.c_str());
                }
                continue;
            }

            if (weight_non_positive) {
                continue;
            }
            if (weight_clamped && should_log("plugin.invalid.weight.range." + kv.first)) {
                SDL_Log("Plugin param weight clamped: id='%s' raw=%f clamped=%f",
                        kv.first.c_str(),
                        out.param_weights.find(kv.first)->second,
                        w);
            }

            const auto p_it = ctx.model->param_index.find(kv.first);
            if (p_it == ctx.model->param_index.end()) {
                continue;
            }

            const int idx = p_it->second;
            if (idx < 0 || idx >= static_cast<int>(ctx.model->parameters.size())) {
                continue;
            }

            auto &param = ctx.model->parameters[static_cast<std::size_t>(idx)].param;
            if (!IsFinite(kv.second)) {
                if (should_log("plugin.invalid.target.nan." + kv.first)) {
                    SDL_Log("Plugin param ignored: id='%s' target is NaN/Inf", kv.first.c_str());
                }
                continue;
            }

            const float clamped_target = std::clamp(kv.second, param.spec().min_value, param.spec().max_value);
            if (clamped_target != kv.second && should_log("plugin.invalid.target.range." + kv.first)) {
                SDL_Log("Plugin param target clamped: id='%s' raw=%f clamped=%f range=[%f,%f]",
                        kv.first.c_str(),
                        kv.second,
                        clamped_target,
                        param.spec().min_value,
                        param.spec().max_value);
            }

            if (ctx.plugin_param_blend_mode == PluginParamBlendMode::Weighted) {
                const float mixed = param.target() * (1.0f - w) + clamped_target * w;
                param.SetTarget(mixed);
            } else {
                param.SetTarget(clamped_target);
            }
        }
    }

    const auto opacity_w_it = out.param_weights.find("window.opacity");
    const auto opacity_t_it = out.param_targets.find("window.opacity");
    if (opacity_w_it != out.param_weights.end() && opacity_t_it != out.param_targets.end() && ctx.window) {
        if (!IsFinite(opacity_w_it->second) || !IsFinite(opacity_t_it->second)) {
            if (should_log("plugin.invalid.window.opacity.nan")) {
                SDL_Log("Plugin window.opacity ignored: weight/target is NaN/Inf");
            }
        } else if (opacity_w_it->second > 0.0f) {
            const float opacity = std::clamp(opacity_t_it->second, 0.05f, 1.0f);
            if (opacity != opacity_t_it->second && should_log("plugin.invalid.window.opacity.range")) {
                SDL_Log("Plugin window.opacity clamped: raw=%f clamped=%f", opacity_t_it->second, opacity);
            }
            if (!SDL_SetWindowOpacity(ctx.window, opacity)) {
                SDL_Log("Plugin SetWindowOpacity failed: %s", SDL_GetError());
            }
        }
    }

    const auto click_w_it = out.param_weights.find("window.click_through");
    const auto click_t_it = out.param_targets.find("window.click_through");
    if (click_w_it != out.param_weights.end() && click_t_it != out.param_targets.end()) {
        if (!IsFinite(click_w_it->second) || !IsFinite(click_t_it->second)) {
            if (should_log("plugin.invalid.window.click.nan")) {
                SDL_Log("Plugin window.click_through ignored: weight/target is NaN/Inf");
            }
        } else if (click_w_it->second > 0.0f) {
            if (ctx.set_click_through) {
                ctx.set_click_through(click_t_it->second >= 0.5f, ctx.set_click_through_user_data);
            }
        }
    }

    const auto debug_w_it = out.param_weights.find("runtime.show_debug_stats");
    const auto debug_t_it = out.param_targets.find("runtime.show_debug_stats");
    if (debug_w_it != out.param_weights.end() && debug_t_it != out.param_targets.end()) {
        if (!IsFinite(debug_w_it->second) || !IsFinite(debug_t_it->second)) {
            if (should_log("plugin.invalid.runtime.debug.nan")) {
                SDL_Log("Plugin runtime.show_debug_stats ignored: weight/target is NaN/Inf");
            }
        } else if (debug_w_it->second > 0.0f && ctx.show_debug_stats) {
            *ctx.show_debug_stats = debug_t_it->second >= 0.5f;
        }
    }

    const auto manual_w_it = out.param_weights.find("runtime.manual_param_mode");
    const auto manual_t_it = out.param_targets.find("runtime.manual_param_mode");
    if (manual_w_it != out.param_weights.end() && manual_t_it != out.param_targets.end()) {
        if (!IsFinite(manual_w_it->second) || !IsFinite(manual_t_it->second)) {
            if (should_log("plugin.invalid.runtime.manual.nan")) {
                SDL_Log("Plugin runtime.manual_param_mode ignored: weight/target is NaN/Inf");
            }
        } else if (manual_w_it->second > 0.0f && ctx.manual_param_mode) {
            *ctx.manual_param_mode = manual_t_it->second >= 0.5f;
            if (ctx.sync_animation_channel_state) {
                ctx.sync_animation_channel_state(ctx.sync_animation_channel_state_user_data);
            }
        }
    }
}

}  // namespace k2d
