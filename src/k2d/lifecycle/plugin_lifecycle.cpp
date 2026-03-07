#include "k2d/lifecycle/plugin_lifecycle.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>

#include "k2d/core/async_logger.h"
#include "k2d/core/json.h"

#include <onnxruntime_cxx_api.h>
#if __has_include(<onnxruntime/core/providers/cuda/cuda_provider_factory.h>)
#include <onnxruntime/core/providers/cuda/cuda_provider_factory.h>
#define K2D_HAS_ORT_CUDA_FACTORY 1
#endif
#if __has_include(<onnxruntime/core/providers/dml/dml_provider_factory.h>)
#include <onnxruntime/core/providers/dml/dml_provider_factory.h>
#define K2D_HAS_ORT_DML_FACTORY 1
#endif

namespace k2d {
namespace {

void SafeHostLog(const PluginHostCallbacks &host, const char *msg) {
    if (host.log) {
        host.log(host.user_data, msg ? msg : "");
        return;
    }
    SDL_Log("[Plugin] %s", msg ? msg : "");
}

class DefaultBehaviorPlugin final : public IBehaviorPlugin {
public:
    PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        if (!out_desc) {
            if (out_error) *out_error = "Init failed: out_desc is null";
            return PluginStatus::InvalidArg;
        }

        host_ = host;
        initialized_ = true;
        elapsed_time_sec_ = 0.0;

        *out_desc = PluginDescriptor{
            .name = "default_behavior",
            .version = "0.1.0",
            .capabilities = "idle_opacity_pulse",
        };

        base_show_debug_stats_ = runtime_cfg.show_debug_stats;
        base_manual_param_mode_ = runtime_cfg.manual_param_mode;
        base_click_through_ = runtime_cfg.click_through;
        base_opacity_ = std::clamp(runtime_cfg.window_opacity, 0.05f, 1.0f);

        SafeHostLog(host_, "DefaultBehaviorPlugin initialized");
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &in,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        if (!initialized_) {
            if (out_error) *out_error = "Update failed: plugin not initialized";
            return PluginStatus::InternalError;
        }

        elapsed_time_sec_ += std::max(0.0f, 1.0f / 60.0f);

        out = BehaviorOutput{};
        const float pulse = 0.1f * SDL_sinf(static_cast<float>(elapsed_time_sec_) * 1.2f);
        out.param_targets["window.opacity"] = std::clamp(base_opacity_ + pulse, 0.05f, 1.0f);
        out.param_weights["window.opacity"] = 1.0f;

        out.param_targets["window.click_through"] = base_click_through_ ? 1.0f : 0.0f;
        out.param_weights["window.click_through"] = 1.0f;

        out.param_targets["runtime.show_debug_stats"] = base_show_debug_stats_ ? 1.0f : 0.0f;
        out.param_weights["runtime.show_debug_stats"] = 1.0f;

        out.param_targets["runtime.manual_param_mode"] = base_manual_param_mode_ ? 1.0f : 0.0f;
        out.param_weights["runtime.manual_param_mode"] = 1.0f;

        out.trigger_idle_shift = (static_cast<int>(elapsed_time_sec_) % 4) == 0;
        out.trigger_blink = (in.user_presence > 0.8f);

        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        if (!initialized_) {
            return;
        }
        initialized_ = false;
        SafeHostLog(host_, "DefaultBehaviorPlugin destroyed");
    }

private:
    PluginHostCallbacks host_{};
    bool initialized_ = false;
    bool base_show_debug_stats_ = true;
    bool base_manual_param_mode_ = false;
    bool base_click_through_ = false;
    float base_opacity_ = 1.0f;
    double elapsed_time_sec_ = 0.0;
};

}  // namespace

void PluginManager::SetPlugin(std::unique_ptr<IBehaviorPlugin> plugin) {
    if (initialized_) {
        Destroy();
    }
    plugin_ = std::move(plugin);
    descriptor_ = PluginDescriptor{};
}

PluginStatus PluginManager::Init(const PluginRuntimeConfig &runtime_cfg,
                                 const PluginHostCallbacks &host,
                                 std::string *out_error) {
    if (!plugin_) {
        if (out_error) *out_error = "PluginManager Init failed: no plugin registered";
        return PluginStatus::InvalidArg;
    }

    descriptor_ = PluginDescriptor{};
    try {
        const PluginStatus st = plugin_->Init(runtime_cfg, host, &descriptor_, out_error);
        initialized_ = (st == PluginStatus::Ok);
        return st;
    } catch (const std::exception &e) {
        initialized_ = false;
        if (out_error) *out_error = std::string("PluginManager Init exception: ") + e.what();
        return PluginStatus::InternalError;
    } catch (...) {
        initialized_ = false;
        if (out_error) *out_error = "PluginManager Init exception: unknown";
        return PluginStatus::InternalError;
    }
}

PluginStatus PluginManager::Update(const PerceptionInput &in,
                                   BehaviorOutput &out,
                                   std::string *out_error) {
    if (!plugin_ || !initialized_) {
        if (out_error) *out_error = "PluginManager Update failed: plugin not ready";
        return PluginStatus::InternalError;
    }

    try {
        return plugin_->Update(in, out, out_error);
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("PluginManager Update exception: ") + e.what();
        return PluginStatus::InternalError;
    } catch (...) {
        if (out_error) *out_error = "PluginManager Update exception: unknown";
        return PluginStatus::InternalError;
    }
}

void PluginManager::Destroy() noexcept {
    if (plugin_) {
        plugin_->Destroy();
    }
    initialized_ = false;
    descriptor_ = PluginDescriptor{};
}

std::unique_ptr<IBehaviorPlugin> CreateDefaultBehaviorPlugin() {
    return std::make_unique<DefaultBehaviorPlugin>();
}

namespace {

bool ApplyBackendPriorityToSessionOptions(const std::vector<std::string> &priority,
                                          Ort::SessionOptions &opts,
                                          std::string *out_backend,
                                          std::string *out_error);

struct PostprocessTrace {
    std::vector<float> values;
    std::vector<std::string> tokens;
    int argmax_index = -1;
    float argmax_value = 0.0f;
    bool threshold_pass = false;
    bool has_threshold = false;
};

PostprocessTrace ExecutePostprocessPipeline(const std::vector<float> &input,
                                            const PluginPostprocessConfig &cfg) {
    PostprocessTrace trace{};
    trace.values = input;

    for (const auto &step : cfg.steps) {
        switch (step.type) {
            case PluginPostprocessStepType::Normalize: {
                float sum = 0.0f;
                for (float v : trace.values) sum += std::fabs(v);
                const float denom = std::max(step.epsilon, sum);
                if (denom > 0.0f) {
                    for (float &v : trace.values) v /= denom;
                }
                break;
            }
            case PluginPostprocessStepType::Tokenize: {
                trace.tokens.clear();
                const std::string delim = step.delimiter.empty() ? std::string(" ") : step.delimiter;
                for (float v : trace.values) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(3) << v;
                    std::string packed = oss.str();
                    std::size_t pos = 0;
                    while (true) {
                        const std::size_t hit = packed.find(delim, pos);
                        if (hit == std::string::npos) {
                            if (pos < packed.size()) trace.tokens.push_back(packed.substr(pos));
                            break;
                        }
                        if (hit > pos) trace.tokens.push_back(packed.substr(pos, hit - pos));
                        pos = hit + delim.size();
                    }
                    if (trace.tokens.empty()) trace.tokens.push_back(packed);
                }
                break;
            }
            case PluginPostprocessStepType::Argmax: {
                if (trace.values.empty()) {
                    trace.argmax_index = -1;
                    trace.argmax_value = 0.0f;
                    break;
                }
                int best_i = 0;
                float best_v = trace.values[0];
                for (int i = 1; i < static_cast<int>(trace.values.size()); ++i) {
                    if (trace.values[static_cast<std::size_t>(i)] > best_v) {
                        best_v = trace.values[static_cast<std::size_t>(i)];
                        best_i = i;
                    }
                }
                trace.argmax_index = best_i;
                trace.argmax_value = best_v;
                break;
            }
            case PluginPostprocessStepType::Threshold: {
                trace.has_threshold = true;
                trace.threshold_pass = trace.argmax_value >= step.threshold;
                break;
            }
            default:
                break;
        }
    }

    return trace;
}

class OnnxBehaviorPlugin final : public IBehaviorPlugin {
public:
    explicit OnnxBehaviorPlugin(PluginArtifactSpec spec)
        : spec_(std::move(spec)), route_config_(BuildRouteConfig(spec_)), postprocess_(spec_.postprocess) {}

    PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        if (!out_desc) {
            if (out_error) *out_error = "Init failed: out_desc is null";
            return PluginStatus::InvalidArg;
        }
        if (spec_.onnx_path.empty() || spec_.config_path.empty()) {
            if (out_error) *out_error = "Init failed: require onnx_path + config_path";
            return PluginStatus::InvalidArg;
        }

        host_ = host;
        initialized_ = false;
        base_show_debug_stats_ = runtime_cfg.show_debug_stats;
        base_manual_param_mode_ = runtime_cfg.manual_param_mode;
        base_click_through_ = runtime_cfg.click_through;
        base_opacity_ = std::clamp(runtime_cfg.window_opacity, 0.05f, 1.0f);

        try {
            session_env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "k2d_plugin_behavior");
            Ort::SessionOptions sess_opts;
            sess_opts.SetIntraOpNumThreads(1);
            sess_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

            std::string backend_apply_err;
            if (!ApplyBackendPriorityToSessionOptions(spec_.backend_priority, sess_opts, &resolved_backend_, &backend_apply_err)) {
                if (out_error) *out_error = "Init failed: backend apply failed: " + backend_apply_err;
                return PluginStatus::InternalError;
            }

            const std::filesystem::path ort_model_path = std::filesystem::path(spec_.onnx_path);
            session_ = std::make_unique<Ort::Session>(*session_env_, ort_model_path.c_str(), sess_opts);
            LogInfo("onnx session created: model_id=%s model_version=%s backend=%s onnx=%s",
                    spec_.model_id.c_str(),
                    spec_.model_version.c_str(),
                    resolved_backend_.c_str(),
                    spec_.onnx_path.c_str());

            expert_sessions_.clear();
            for (const auto &expert : spec_.expert_models) {
                if (expert.onnx_path.empty()) {
                    continue;
                }
                const std::filesystem::path expert_path = std::filesystem::path(expert.onnx_path);
                expert_sessions_.push_back(ExpertRuntime{
                    .route_name = expert.route_name,
                    .fusion_weight = std::clamp(expert.fusion_weight, 0.0f, 1.0f),
                    .session = std::make_unique<Ort::Session>(*session_env_, expert_path.c_str(), sess_opts),
                });
            }
        } catch (const std::exception &e) {
            if (out_error) {
                *out_error = "Init failed: onnx session create failed (backend=" + resolved_backend_ + "): " + e.what();
            }
            LogError("onnx session create failed: model_id=%s backend=%s err=%s",
                     spec_.model_id.c_str(),
                     resolved_backend_.c_str(),
                     e.what());
            session_.reset();
            session_env_.reset();
            return PluginStatus::InternalError;
        }

        *out_desc = PluginDescriptor{
            .name = "onnx_behavior",
            .version = "0.1.0",
            .capabilities = "onnx+config,multi-model-coop",
        };
        initialized_ = true;
        SafeHostLog(host_, "OnnxBehaviorPlugin initialized");
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &in,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        if (!initialized_) {
            if (out_error) *out_error = "Update failed: plugin not initialized";
            return PluginStatus::InternalError;
        }

        out = BehaviorOutput{};
        const float presence = std::clamp(in.vision.user_presence, 0.0f, 1.0f);

        const RouteMatch route = ResolveRoute(in);
        const float opacity_bias = route.opacity_bias;

        std::vector<float> input_tensor_values = {
            static_cast<float>(in.audio.level),
            static_cast<float>(in.audio.pitch_hz),
            static_cast<float>(in.audio.vad_prob),
            static_cast<float>(in.vision.user_presence),
            static_cast<float>(in.vision.head_yaw_deg),
            static_cast<float>(in.vision.head_pitch_deg),
            static_cast<float>(in.vision.head_roll_deg),
            static_cast<float>(in.state.window_visible ? 1.0f : 0.0f),
        };

        auto run_session = [&](Ort::Session &sess, std::vector<float> &out_values, std::string *run_err) -> bool {
            try {
                const std::size_t expected_features = static_cast<std::size_t>(
                    std::max<std::int64_t>(1, spec_.tensor_schema.feature_dim));
                if (input_tensor_values.size() != expected_features) {
                    if (run_err) {
                        *run_err = "input feature mismatch: expected=" + std::to_string(expected_features) +
                                   " actual=" + std::to_string(input_tensor_values.size());
                    }
                    return false;
                }

                const std::array<int64_t, 2> input_shape = {
                    std::max<std::int64_t>(1, spec_.tensor_schema.batch),
                    std::max<std::int64_t>(1, spec_.tensor_schema.feature_dim),
                };
                Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                Ort::Value input_tensor = Ort::Value::CreateTensor<float>(mem_info,
                                                                          input_tensor_values.data(),
                                                                          input_tensor_values.size(),
                                                                          input_shape.data(),
                                                                          input_shape.size());
                const char *input_names[] = {spec_.tensor_schema.input_name.c_str()};
                const char *output_names[] = {spec_.tensor_schema.output_name.c_str()};
                auto outputs = sess.Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
                if (outputs.empty() || !outputs[0].IsTensor()) {
                    if (run_err) *run_err = "ort output is empty or not tensor";
                    return false;
                }
                float *data = outputs[0].GetTensorMutableData<float>();
                auto type_info = outputs[0].GetTensorTypeAndShapeInfo();
                const std::size_t elem_count = type_info.GetElementCount();
                out_values.assign(data, data + elem_count);
                return true;
            } catch (const std::exception &e) {
                if (run_err) *run_err = e.what();
                return false;
            }
        };

        std::vector<float> main_logits;
        std::string ort_err;
        if (session_ && !run_session(*session_, main_logits, &ort_err)) {
            if (out_error) *out_error = std::string("ORT Run failed: ") + ort_err;
            return PluginStatus::InternalError;
        }

        std::vector<float> fused_logits = main_logits;
        int active_expert_count = 0;

        struct ExpertForwardResult {
            std::size_t expert_index = 0;
            std::vector<float> logits;
            float gating_score = 0.0f;
            float softmax_weight = 0.0f;
        };

        std::vector<ExpertForwardResult> expert_results;
        const float structured_confidence = std::clamp(
            0.5f * (in.routing.primary_structured_confidence + in.routing.secondary_structured_confidence),
            0.0f,
            1.0f);
        const float gating_temperature = std::max(0.05f, route_config_.expert_gating_temperature);

        for (std::size_t expert_idx = 0; expert_idx < expert_sessions_.size(); ++expert_idx) {
            auto &expert = expert_sessions_[expert_idx];
            expert.last_gating_score = 0.0f;
            expert.last_softmax_weight = 0.0f;
            if (!expert.session) {
                continue;
            }
            if (!expert.route_name.empty() && expert.route_name != route.name) {
                continue;
            }
            std::vector<float> expert_logits;
            if (!run_session(*expert.session, expert_logits, nullptr)) {
                continue;
            }
            if (expert_logits.size() != fused_logits.size()) {
                continue;
            }

            const float route_alignment = expert.route_name.empty() ? 0.0f : 1.0f;
            const float gating_score = route.total_score + structured_confidence + route_alignment;
            expert.last_gating_score = gating_score;
            expert_results.push_back(ExpertForwardResult{
                .expert_index = expert_idx,
                .logits = std::move(expert_logits),
                .gating_score = gating_score,
                .softmax_weight = 0.0f,
            });
        }

        if (!expert_results.empty()) {
            float max_logit = expert_results.front().gating_score / gating_temperature;
            for (const auto &r : expert_results) {
                max_logit = std::max(max_logit, r.gating_score / gating_temperature);
            }

            float sum_exp = 0.0f;
            for (auto &r : expert_results) {
                r.softmax_weight = std::exp((r.gating_score / gating_temperature) - max_logit);
                sum_exp += r.softmax_weight;
            }

            if (sum_exp > 0.0f) {
                for (auto &r : expert_results) {
                    r.softmax_weight /= sum_exp;
                    expert_sessions_[r.expert_index].last_softmax_weight = r.softmax_weight;
                }

                std::vector<float> blended_expert_logits(fused_logits.size(), 0.0f);
                for (const auto &r : expert_results) {
                    for (std::size_t j = 0; j < blended_expert_logits.size(); ++j) {
                        blended_expert_logits[j] += r.logits[j] * r.softmax_weight;
                    }
                }

                float expert_mix_ratio = 0.0f;
                for (const auto &r : expert_results) {
                    expert_mix_ratio += r.softmax_weight *
                                        std::clamp(expert_sessions_[r.expert_index].fusion_weight, 0.0f, 1.0f);
                }
                expert_mix_ratio = std::clamp(expert_mix_ratio, 0.0f, 1.0f);

                for (std::size_t j = 0; j < fused_logits.size(); ++j) {
                    fused_logits[j] = fused_logits[j] * (1.0f - expert_mix_ratio) +
                                      blended_expert_logits[j] * expert_mix_ratio;
                }
                active_expert_count = static_cast<int>(expert_results.size());
            }
        }
        out.event_scores["plugin.route." + route.name] = 1.0f;
        out.event_scores["plugin.route.trace.scene_score"] = route.scene_score;
        out.event_scores["plugin.route.trace.task_score"] = route.task_score;
        out.event_scores["plugin.route.trace.structured_score"] = route.structured_score;
        out.event_scores["plugin.route.trace.presence_score"] = route.presence_score;
        out.event_scores["plugin.route.trace.total_score"] = route.total_score;
        for (std::size_t i = 0; i < route.rejected.size(); ++i) {
            const auto &cand = route.rejected[i];
            const std::string prefix = "plugin.route.trace.rejected." + std::to_string(i) + ".";
            out.event_scores[prefix + cand.name] = cand.total_score;
            out.event_scores[prefix + "scene_score"] = cand.scene_score;
            out.event_scores[prefix + "task_score"] = cand.task_score;
            out.event_scores[prefix + "structured_score"] = cand.structured_score;
            out.event_scores[prefix + "presence_score"] = cand.presence_score;
        }
        {
            std::ostringstream oss;
            oss << "route_selected=" << route.name
                << " scene_score=" << route.scene_score
                << " task_score=" << route.task_score
                << " structured_score=" << route.structured_score
                << " presence_score=" << route.presence_score
                << " total_score=" << route.total_score;
            if (!route.rejected.empty()) {
                oss << " rejected=";
                for (std::size_t i = 0; i < route.rejected.size(); ++i) {
                    if (i > 0) oss << ",";
                    oss << route.rejected[i].name << ":" << route.rejected[i].total_score;
                }
            }
            LogInfo("plugin route trace: %s", oss.str().c_str());
        }

        out.param_targets["window.opacity"] = std::clamp(base_opacity_ + opacity_bias + presence * 0.05f, 0.05f, 1.0f);
        out.param_weights["window.opacity"] = 1.0f;
        out.param_targets["window.click_through"] = base_click_through_ ? 1.0f : 0.0f;
        out.param_weights["window.click_through"] = 1.0f;
        out.param_targets["runtime.show_debug_stats"] = base_show_debug_stats_ ? 1.0f : 0.0f;
        out.param_weights["runtime.show_debug_stats"] = 1.0f;
        out.param_targets["runtime.manual_param_mode"] = base_manual_param_mode_ ? 1.0f : 0.0f;
        out.param_weights["runtime.manual_param_mode"] = 1.0f;
        out.event_scores["plugin.models.count"] = static_cast<float>(1 + spec_.expert_models.size());
        out.event_scores["plugin.experts.active_count"] = static_cast<float>(active_expert_count);
        out.event_scores["plugin.expert.gating.temperature"] = gating_temperature;
        out.event_scores["plugin.route.selected"] = (route.name == route_config_.default_route ? 0.0f : 1.0f);

        if (fused_logits.empty()) {
            fused_logits = {
                std::clamp(presence, 0.0f, 1.0f),
                std::clamp(1.0f - presence, 0.0f, 1.0f),
                std::clamp(0.25f + opacity_bias, 0.0f, 1.0f),
            };
        }
        const PostprocessTrace trace = ExecutePostprocessPipeline(fused_logits, postprocess_);
        out.event_scores["plugin.postprocess.argmax_index"] = static_cast<float>(trace.argmax_index);
        out.event_scores["plugin.postprocess.argmax_value"] = trace.argmax_value;
        out.event_scores["plugin.postprocess.threshold_pass"] = trace.threshold_pass ? 1.0f : 0.0f;

        out.trigger_blink = trace.has_threshold ? trace.threshold_pass : (presence > 0.8f);
        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        LogInfo("onnx plugin destroy: model_id=%s backend=%s", spec_.model_id.c_str(), resolved_backend_.c_str());
        session_.reset();
        session_env_.reset();
        initialized_ = false;
    }

private:
    struct ExpertRuntime {
        std::string route_name;
        float fusion_weight = 0.35f;
        float last_gating_score = 0.0f;
        float last_softmax_weight = 0.0f;
        std::unique_ptr<Ort::Session> session;
    };

    struct RouteCandidateScore {
        std::string name;
        float opacity_bias = 0.0f;
        float scene_score = 0.0f;
        float task_score = 0.0f;
        float structured_score = 0.0f;
        float presence_score = 0.0f;
        float total_score = 0.0f;
    };

    struct RouteMatch {
        std::string name = "unknown";
        float opacity_bias = 0.0f;
        float scene_score = 0.0f;
        float task_score = 0.0f;
        float structured_score = 0.0f;
        float presence_score = 0.0f;
        float total_score = 0.0f;
        std::vector<RouteCandidateScore> rejected;
    };

    static std::string ToLowerCopy(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    }

    static int CountKeywordHits(const std::string &text_lower, const std::vector<std::string> &keywords) {
        int hits = 0;
        for (const std::string &kw : keywords) {
            if (kw.empty()) continue;
            if (text_lower.find(ToLowerCopy(kw)) != std::string::npos) {
                hits += 1;
            }
        }
        return hits;
    }

    static PluginRouteConfig BuildRouteConfig(const PluginArtifactSpec &spec) {
        if (!spec.route_config.rules.empty()) {
            PluginRouteConfig cfg = spec.route_config;
            if (cfg.default_route.empty()) cfg.default_route = "unknown";
            return cfg;
        }

        PluginRouteConfig fallback;
        fallback.default_route = "unknown";
        return fallback;
    }

    RouteMatch ResolveRoute(const PerceptionInput &in) {
        const std::string scene = ToLowerCopy(in.scene_label);
        const std::string task = ToLowerCopy(in.task_label);
        const float presence = std::clamp(in.vision.user_presence, 0.0f, 1.0f);
        const bool visible_hint = in.state.window_visible;
        const bool click_through_hint = in.state.click_through;

        RouteMatch best{};
        best.name = route_config_.default_route.empty() ? std::string("unknown") : route_config_.default_route;

        std::vector<RouteCandidateScore> signal_candidates;
        std::vector<RouteCandidateScore> all_rejected;
        for (const PluginRouteRule &rule : route_config_.rules) {
            if (rule.name.empty()) continue;

            const int scene_hits = CountKeywordHits(scene, rule.scene_keywords);
            const int task_hits = CountKeywordHits(task, rule.task_keywords);
            const float scene_score = static_cast<float>(scene_hits) * rule.scene_weight;
            const float task_score = static_cast<float>(task_hits) * rule.task_weight;
            const float structured_score =
                std::clamp(in.routing.primary_structured_confidence, 0.0f, 1.0f) * rule.primary_weight +
                std::clamp(in.routing.secondary_structured_confidence, 0.0f, 1.0f) * rule.secondary_weight;
            const float presence_score = presence * (visible_hint ? 0.35f : 0.15f) + (click_through_hint ? -0.10f : 0.05f);
            const float total_score = scene_score + task_score + structured_score * rule.structured_weight + presence_score;

            RouteCandidateScore cand{
                .name = rule.name,
                .opacity_bias = rule.opacity_bias,
                .scene_score = scene_score,
                .task_score = task_score,
                .structured_score = structured_score,
                .presence_score = presence_score,
                .total_score = total_score,
            };

            const bool has_signal = scene_hits > 0 || task_hits > 0 || structured_score > 0.0f;
            if (has_signal) {
                signal_candidates.push_back(cand);
            } else {
                all_rejected.push_back(cand);
            }
        }

        std::sort(signal_candidates.begin(), signal_candidates.end(), [](const RouteCandidateScore &a, const RouteCandidateScore &b) {
            if (std::abs(a.total_score - b.total_score) < 1e-6f) {
                return a.scene_score > b.scene_score;
            }
            return a.total_score > b.total_score;
        });

        auto find_candidate = [](const std::vector<RouteCandidateScore> &cands, const std::string &name) -> const RouteCandidateScore * {
            for (const auto &cand : cands) {
                if (cand.name == name) return &cand;
            }
            return nullptr;
        };

        const RouteCandidateScore *top_signal = signal_candidates.empty() ? nullptr : &signal_candidates.front();

        if (route_hold_frames_left_ > 0) {
            --route_hold_frames_left_;
        }
        if (route_cooldown_frames_left_ > 0) {
            --route_cooldown_frames_left_;
        }

        if (last_selected_route_.empty()) {
            last_selected_route_ = best.name;
        }

        const RouteCandidateScore *current_cand = find_candidate(signal_candidates, last_selected_route_);
        const float current_total_score = current_cand ? current_cand->total_score : 0.0f;

        if (top_signal && top_signal->total_score >= route_config_.activation_threshold) {
            const bool candidate_is_new = top_signal->name != last_selected_route_;
            const bool hold_ready = route_hold_frames_left_ <= 0;
            const bool cooldown_ready = route_cooldown_frames_left_ <= 0;
            const bool margin_ready = (top_signal->total_score - current_total_score) >= route_config_.switch_score_margin;

            if (!candidate_is_new) {
                last_selected_route_ = top_signal->name;
            } else if (hold_ready && cooldown_ready && margin_ready) {
                last_selected_route_ = top_signal->name;
                route_hold_frames_left_ = std::max(0, route_config_.switch_hold_frames);
                route_cooldown_frames_left_ = std::max(0, route_config_.switch_cooldown_frames);
            }
        }

        const RouteCandidateScore *selected_cand = find_candidate(signal_candidates, last_selected_route_);
        if (selected_cand) {
            best.name = selected_cand->name;
            best.opacity_bias = selected_cand->opacity_bias;
            best.scene_score = selected_cand->scene_score;
            best.task_score = selected_cand->task_score;
            best.structured_score = selected_cand->structured_score;
            best.presence_score = selected_cand->presence_score;
            best.total_score = selected_cand->total_score;
        } else {
            best.name = route_config_.default_route.empty() ? std::string("unknown") : route_config_.default_route;
        }

        for (const auto &cand : signal_candidates) {
            if (cand.name != best.name) {
                all_rejected.push_back(cand);
            }
        }
        best.rejected = std::move(all_rejected);
        std::sort(best.rejected.begin(), best.rejected.end(), [](const RouteCandidateScore &a, const RouteCandidateScore &b) {
            return a.total_score > b.total_score;
        });
        return best;
    }

    PluginArtifactSpec spec_{};
    PluginRouteConfig route_config_{};
    PluginHostCallbacks host_{};
    bool initialized_ = false;
    bool base_show_debug_stats_ = true;
    bool base_manual_param_mode_ = false;
    bool base_click_through_ = false;
    float base_opacity_ = 1.0f;
    PluginPostprocessConfig postprocess_{};
    std::string resolved_backend_ = "cpu";
    std::unique_ptr<Ort::Env> session_env_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<ExpertRuntime> expert_sessions_;
    std::string last_selected_route_;
    int route_hold_frames_left_ = 0;
    int route_cooldown_frames_left_ = 0;
};

std::string ReadTextFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::string Fnv1a64OfFileHex(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};

    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;

    char buf[8192];
    while (ifs) {
        ifs.read(buf, sizeof(buf));
        const std::streamsize n = ifs.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buf[i]);
            hash *= kPrime;
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

bool JsonArrayAllString(const JsonValue *v) {
    if (!v || !v->isArray() || !v->asArray()) return false;
    for (const auto &item : *v->asArray()) {
        if (!item.isString()) return false;
    }
    return true;
}

std::vector<std::string> NormalizeBackendPriority(const JsonValue *arr) {
    std::vector<std::string> out;
    if (!arr || !arr->isArray() || !arr->asArray()) {
        return {"cpu"};
    }
    for (const auto &v : *arr->asArray()) {
        if (!v.isString() || !v.asString()) continue;
        std::string b = *v.asString();
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (b == "dml") b = "directml";
        if (b == "cpu" || b == "cuda" || b == "directml") {
            if (std::find(out.begin(), out.end(), b) == out.end()) {
                out.push_back(b);
            }
        }
    }
    if (out.empty()) out.push_back("cpu");
    return out;
}

bool ApplyBackendPriorityToSessionOptions(const std::vector<std::string> &priority,
                                          Ort::SessionOptions &opts,
                                          std::string *out_backend,
                                          std::string *out_error) {
    if (out_backend) *out_backend = "cpu";
    if (out_error) out_error->clear();

    for (const auto &b : priority) {
        if (b == "cuda") {
#if defined(K2D_HAS_ORT_CUDA_FACTORY)
            OrtCUDAProviderOptions cuda_opts{};
            OrtStatus *st = OrtSessionOptionsAppendExecutionProvider_CUDA(opts, &cuda_opts);
            if (st == nullptr) {
                if (out_backend) *out_backend = "cuda";
                if (out_error) out_error->clear();
                LogInfo("plugin backend selected: cuda");
                return true;
            }
            if (out_error) *out_error = "append cuda provider failed";
            LogError("plugin backend cuda append failed, fallback to next");
#else
            if (out_error) *out_error = "cuda provider not available in this build";
            LogInfo("plugin backend cuda not available in this build");
#endif
            continue;
        }

        if (b == "directml") {
#if defined(K2D_HAS_ORT_DML_FACTORY)
            OrtStatus *st = OrtSessionOptionsAppendExecutionProvider_DML(opts, 0);
            if (st == nullptr) {
                if (out_backend) *out_backend = "directml";
                if (out_error) out_error->clear();
                LogInfo("plugin backend selected: directml");
                return true;
            }
            if (out_error) *out_error = "append directml provider failed";
            LogError("plugin backend directml append failed, fallback to next");
#else
            if (out_error) *out_error = "directml provider not available in this build";
            LogInfo("plugin backend directml not available in this build");
#endif
            continue;
        }

        if (b == "cpu") {
            if (out_backend) *out_backend = "cpu";
            if (out_error) out_error->clear();
            LogInfo("plugin backend selected: cpu");
            return true;
        }
    }

    if (out_backend) *out_backend = "cpu";
    if (out_error) out_error->clear();
    LogInfo("plugin backend defaulted to cpu");
    return true;
}

void ApplyWorkerTuningFromJsonObject(const JsonValue &worker, PluginArtifactSpec::WorkerTuning &cfg) {
    cfg.update_hz = static_cast<int>(worker.getNumber("update_hz").value_or(cfg.update_hz));
    cfg.frame_budget_ms = static_cast<int>(worker.getNumber("frame_budget_ms").value_or(cfg.frame_budget_ms));
    cfg.timeout_degrade_threshold = static_cast<int>(
        worker.getNumber("timeout_degrade_threshold").value_or(cfg.timeout_degrade_threshold));
    cfg.recover_after_consecutive_successes = static_cast<int>(
        worker.getNumber("recover_after_consecutive_successes").value_or(cfg.recover_after_consecutive_successes));
    cfg.avg_latency_budget_ms = worker.getNumber("avg_latency_budget_ms").value_or(cfg.avg_latency_budget_ms);
    cfg.latency_budget_window_size = static_cast<std::size_t>(
        worker.getNumber("latency_budget_window_size").value_or(static_cast<double>(cfg.latency_budget_window_size)));
    cfg.disable_after_consecutive_failures = static_cast<int>(
        worker.getNumber("disable_after_consecutive_failures").value_or(cfg.disable_after_consecutive_failures));
    cfg.auto_recover_after_ms = static_cast<int>(
        worker.getNumber("auto_recover_after_ms").value_or(cfg.auto_recover_after_ms));

    if (const JsonValue *steps = worker.get("degrade_hz_steps"); steps && steps->isArray() && steps->asArray()) {
        std::vector<int> parsed;
        for (const auto &item : *steps->asArray()) {
            if (!item.isNumber() || !item.asNumber()) {
                continue;
            }
            const int hz = static_cast<int>(*item.asNumber());
            if (hz > 0) {
                parsed.push_back(hz);
            }
        }
        if (!parsed.empty()) {
            cfg.degrade_hz_steps = std::move(parsed);
        }
    }
}

bool ValidatePluginConfig(const JsonValue &root,
                          const std::string &config_path,
                          PluginArtifactSpec *out_spec,
                          std::string *out_error) {
    if (!root.isObject() || !out_spec) {
        if (out_error) *out_error = "plugin config invalid: root must be object";
        return false;
    }

    auto fail = [&](const std::string &msg) {
        if (out_error) *out_error = msg;
        return false;
    };

    // schema_version: required int, currently only v1 accepted.
    const std::optional<double> schema_version = root.getNumber("schema_version");
    if (!schema_version.has_value()) {
        return fail("plugin config missing required field: schema_version");
    }
    if (*schema_version < 1.0 || *schema_version > 1.0 || std::floor(*schema_version) != *schema_version) {
        return fail("plugin config invalid field: schema_version must be integer 1");
    }

    const std::optional<std::string> onnx = root.getString("onnx");
    if (!onnx.has_value() || onnx->empty()) {
        return fail("plugin config missing required field: onnx");
    }

    std::filesystem::path cfg_dir = std::filesystem::path(config_path).parent_path();
    std::filesystem::path onnx_path = std::filesystem::path(*onnx);
    if (onnx_path.is_relative()) {
        onnx_path = (cfg_dir / onnx_path).lexically_normal();
    }
    std::error_code ec;
    if (!std::filesystem::exists(onnx_path, ec) || ec) {
        return fail("plugin config invalid field: onnx file not found -> " + onnx_path.generic_string());
    }

    const std::optional<std::string> model_id = root.getString("model_id");
    if (!model_id.has_value() || model_id->empty()) {
        return fail("plugin config missing required field: model_id");
    }

    const std::optional<std::string> model_version = root.getString("model_version");
    if (!model_version.has_value() || model_version->empty()) {
        return fail("plugin config missing required field: model_version");
    }

    const std::optional<std::string> onnx_checksum = root.getString("onnx_checksum_fnv1a64");
    if (!onnx_checksum.has_value() || onnx_checksum->empty()) {
        return fail("plugin config missing required field: onnx_checksum_fnv1a64");
    }

    const std::string actual_hash = Fnv1a64OfFileHex(onnx_path.generic_string());
    if (actual_hash.empty()) {
        return fail("plugin config invalid: failed to hash onnx file");
    }
    if (actual_hash != *onnx_checksum) {
        return fail("plugin config atomic binding mismatch: onnx_checksum_fnv1a64 expected=" + *onnx_checksum +
                    " actual=" + actual_hash);
    }

    if (const JsonValue *extra = root.get("extra_onnx"); extra) {
        if (!JsonArrayAllString(extra)) {
            return fail("plugin config invalid field: extra_onnx must be string array");
        }
    }

    const JsonValue *routing = root.get("routing");
    if (!routing || !routing->isObject()) {
        return fail("plugin config missing required object: routing");
    }

    const std::optional<std::string> default_route = routing->getString("default_route");
    if (!default_route.has_value() || default_route->empty()) {
        return fail("plugin config invalid field: routing.default_route must be non-empty string");
    }

    const JsonValue *rules = routing->get("rules");
    if (!rules || !rules->isArray() || !rules->asArray() || rules->asArray()->empty()) {
        return fail("plugin config invalid field: routing.rules must be non-empty array");
    }

    PluginArtifactSpec spec{};
    spec.config_path = config_path;
    spec.onnx_path = onnx_path.generic_string();
    spec.backend_priority = NormalizeBackendPriority(root.get("backend_priority"));

    if (const JsonValue *arr = root.get("extra_onnx"); arr && arr->isArray()) {
        for (const auto &v : *arr->asArray()) {
            if (!v.isString() || !v.asString()) continue;
            std::filesystem::path p = std::filesystem::path(*v.asString());
            if (p.is_relative()) p = (cfg_dir / p).lexically_normal();
            std::error_code extra_ec;
            if (!std::filesystem::exists(p, extra_ec) || extra_ec) {
                return fail("plugin config invalid field: extra_onnx file not found -> " + p.generic_string());
            }
            spec.extra_onnx_paths.push_back(p.generic_string());
        }
    }

    if (const JsonValue *experts = root.get("expert_models"); experts) {
        if (!experts->isArray() || !experts->asArray()) {
            return fail("plugin config invalid field: expert_models must be array");
        }
        for (const auto &ev : *experts->asArray()) {
            if (!ev.isObject()) {
                return fail("plugin config invalid field: expert_models[] must be object");
            }
            const std::optional<std::string> route_name = ev.getString("route_name");
            const std::optional<std::string> onnx_rel = ev.getString("onnx");
            if (!route_name.has_value() || route_name->empty()) {
                return fail("plugin config invalid field: expert_models[].route_name must be non-empty string");
            }
            if (!onnx_rel.has_value() || onnx_rel->empty()) {
                return fail("plugin config invalid field: expert_models[].onnx must be non-empty string");
            }
            std::filesystem::path p = std::filesystem::path(*onnx_rel);
            if (p.is_relative()) p = (cfg_dir / p).lexically_normal();
            std::error_code expert_ec;
            if (!std::filesystem::exists(p, expert_ec) || expert_ec) {
                return fail("plugin config invalid field: expert_models[].onnx file not found -> " + p.generic_string());
            }
            PluginArtifactSpec::ExpertModelSpec expert{};
            expert.route_name = *route_name;
            expert.onnx_path = p.generic_string();
            expert.fusion_weight = static_cast<float>(ev.getNumber("fusion_weight").value_or(static_cast<double>(expert.fusion_weight)));
            if (expert.fusion_weight < 0.0f || expert.fusion_weight > 1.0f) {
                return fail("plugin config invalid field: expert_models[].fusion_weight must be in [0,1]");
            }
            spec.expert_models.push_back(std::move(expert));
        }
    }

    spec.model_id = *model_id;
    spec.model_version = *model_version;
    spec.onnx_checksum_fnv1a64 = *onnx_checksum;

    const JsonValue *io_contract = root.get("io_contract");
    if (!io_contract || !io_contract->isObject()) {
        return fail("plugin config missing required object: io_contract");
    }
    const JsonValue *expected_inputs = io_contract->get("inputs");
    const JsonValue *expected_outputs = io_contract->get("outputs");
    if (!JsonArrayAllString(expected_inputs) || !JsonArrayAllString(expected_outputs) ||
        !expected_inputs->asArray() || !expected_outputs->asArray() ||
        expected_inputs->asArray()->empty() || expected_outputs->asArray()->empty()) {
        return fail("plugin config invalid field: io_contract.inputs/outputs must be non-empty string arrays");
    }

    std::vector<std::string> model_inputs;
    std::vector<std::string> model_outputs;
    try {
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "k2d_io_contract");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);

        std::string backend_apply_err;
        if (!ApplyBackendPriorityToSessionOptions(spec.backend_priority, opts, &spec.resolved_backend, &backend_apply_err)) {
            return fail("plugin backend apply failed: " + backend_apply_err);
        }

        const std::filesystem::path ort_model_path = onnx_path;
        Ort::Session session(env, ort_model_path.c_str(), opts);

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t in_count = session.GetInputCount();
        const size_t out_count = session.GetOutputCount();
        model_inputs.reserve(in_count);
        model_outputs.reserve(out_count);
        for (size_t i = 0; i < in_count; ++i) {
            auto name = session.GetInputNameAllocated(i, allocator);
            model_inputs.emplace_back(name ? name.get() : "");
        }
        for (size_t i = 0; i < out_count; ++i) {
            auto name = session.GetOutputNameAllocated(i, allocator);
            model_outputs.emplace_back(name ? name.get() : "");
        }
    } catch (const std::exception &e) {
        return fail(std::string("plugin config io_contract check failed: ") + e.what());
    }

    auto missingName = [](const std::vector<std::string> &expected, const std::vector<std::string> &actual) -> std::string {
        for (const auto &x : expected) {
            if (std::find(actual.begin(), actual.end(), x) == actual.end()) {
                return x;
            }
        }
        return {};
    };

    std::vector<std::string> exp_inputs;
    std::vector<std::string> exp_outputs;
    for (const auto &v : *expected_inputs->asArray()) exp_inputs.push_back(*v.asString());
    for (const auto &v : *expected_outputs->asArray()) exp_outputs.push_back(*v.asString());

    if (const std::string miss = missingName(exp_inputs, model_inputs); !miss.empty()) {
        return fail("plugin config io_contract mismatch: missing input name -> " + miss);
    }
    if (const std::string miss = missingName(exp_outputs, model_outputs); !miss.empty()) {
        return fail("plugin config io_contract mismatch: missing output name -> " + miss);
    }

    // postprocess: 可配置后处理流水线。
    if (const JsonValue *pp = root.get("postprocess"); pp && pp->isObject()) {
        if (const JsonValue *steps = pp->get("steps"); steps && steps->isArray() && steps->asArray()) {
            for (const auto &sv : *steps->asArray()) {
                if (!sv.isObject()) {
                    return fail("plugin config invalid field: postprocess.steps[] must be object");
                }
                auto type = sv.getString("type");
                if (!type.has_value() || type->empty()) {
                    return fail("plugin config invalid field: postprocess.steps[].type is required");
                }
                PluginPostprocessStep step{};
                step.name = sv.getString("name").value_or(*type);
                if (*type == "normalize") {
                    step.type = PluginPostprocessStepType::Normalize;
                    step.epsilon = static_cast<float>(sv.getNumber("epsilon").value_or(1e-6));
                    if (!(step.epsilon > 0.0f)) {
                        return fail("plugin config invalid field: normalize.epsilon must be > 0");
                    }
                } else if (*type == "tokenize") {
                    step.type = PluginPostprocessStepType::Tokenize;
                    step.delimiter = sv.getString("delimiter").value_or(" ");
                } else if (*type == "argmax") {
                    step.type = PluginPostprocessStepType::Argmax;
                } else if (*type == "threshold") {
                    step.type = PluginPostprocessStepType::Threshold;
                    step.threshold = static_cast<float>(sv.getNumber("threshold").value_or(0.5));
                    if (step.threshold < 0.0f || step.threshold > 1.0f) {
                        return fail("plugin config invalid field: threshold.threshold must be in [0,1]");
                    }
                } else {
                    return fail("plugin config invalid field: unknown postprocess step type -> " + *type);
                }
                spec.postprocess.steps.push_back(std::move(step));
            }
        }
    }

    if (spec.postprocess.steps.empty()) {
        spec.postprocess.steps = {
            PluginPostprocessStep{.type = PluginPostprocessStepType::Normalize, .name = "normalize", .epsilon = 1e-6f},
            PluginPostprocessStep{.type = PluginPostprocessStepType::Argmax, .name = "argmax"},
            PluginPostprocessStep{.type = PluginPostprocessStepType::Threshold, .name = "threshold", .threshold = 0.5f},
        };
    }

    if (const JsonValue *tensor_schema = root.get("tensor_schema"); tensor_schema && tensor_schema->isObject()) {
        spec.tensor_schema.input_name = tensor_schema->getString("input_name").value_or(spec.tensor_schema.input_name);
        spec.tensor_schema.output_name = tensor_schema->getString("output_name").value_or(spec.tensor_schema.output_name);
        spec.tensor_schema.batch = static_cast<std::int64_t>(
            tensor_schema->getNumber("batch").value_or(static_cast<double>(spec.tensor_schema.batch)));
        spec.tensor_schema.feature_dim = static_cast<std::int64_t>(
            tensor_schema->getNumber("feature_dim").value_or(static_cast<double>(spec.tensor_schema.feature_dim)));
        if (spec.tensor_schema.input_name.empty() || spec.tensor_schema.output_name.empty()) {
            return fail("plugin config invalid field: tensor_schema input/output name must be non-empty string");
        }
        if (spec.tensor_schema.batch <= 0 || spec.tensor_schema.feature_dim <= 0) {
            return fail("plugin config invalid field: tensor_schema batch/feature_dim must be > 0");
        }
    }

    if (const JsonValue *worker = root.get("worker"); worker && worker->isObject()) {
        ApplyWorkerTuningFromJsonObject(*worker, spec.worker_tuning);
    }

    spec.route_config.default_route = *default_route;
    spec.route_config.switch_hold_frames = static_cast<int>(routing->getNumber("switch_hold_frames").value_or(static_cast<double>(spec.route_config.switch_hold_frames)));
    spec.route_config.switch_cooldown_frames = static_cast<int>(routing->getNumber("switch_cooldown_frames").value_or(static_cast<double>(spec.route_config.switch_cooldown_frames)));
    spec.route_config.switch_score_margin = static_cast<float>(routing->getNumber("switch_score_margin").value_or(static_cast<double>(spec.route_config.switch_score_margin)));
    spec.route_config.activation_threshold = static_cast<float>(routing->getNumber("activation_threshold").value_or(static_cast<double>(spec.route_config.activation_threshold)));
    spec.route_config.switch_hold_frames = std::max(0, spec.route_config.switch_hold_frames);
    spec.route_config.switch_cooldown_frames = std::max(0, spec.route_config.switch_cooldown_frames);
    spec.route_config.switch_score_margin = std::max(0.0f, spec.route_config.switch_score_margin);
    spec.route_config.activation_threshold = std::max(0.0f, spec.route_config.activation_threshold);
    spec.route_config.expert_gating_temperature = static_cast<float>(
        routing->getNumber("expert_gating_temperature").value_or(static_cast<double>(spec.route_config.expert_gating_temperature)));
    spec.route_config.expert_gating_temperature = std::max(0.05f, spec.route_config.expert_gating_temperature);
    for (const auto &rv : *rules->asArray()) {
        if (!rv.isObject()) {
            return fail("plugin config invalid field: routing.rules item must be object");
        }
        PluginRouteRule rule{};
        auto name = rv.getString("name");
        if (!name.has_value() || name->empty()) {
            return fail("plugin config invalid field: routing.rules[].name must be non-empty string");
        }
        rule.name = *name;

        const std::optional<double> bias = rv.getNumber("opacity_bias");
        if (!bias.has_value() || *bias < -1.0 || *bias > 1.0) {
            return fail("plugin config invalid field: routing.rules[].opacity_bias must be in [-1,1]");
        }
        rule.opacity_bias = static_cast<float>(*bias);

        const JsonValue *scene_kw = rv.get("scene_keywords");
        const JsonValue *task_kw = rv.get("task_keywords");
        if (!JsonArrayAllString(scene_kw) || !JsonArrayAllString(task_kw)) {
            return fail("plugin config invalid field: routing.rules[] keywords must be string arrays");
        }

        for (const auto &kw : *scene_kw->asArray()) {
            if (kw.asString() && !kw.asString()->empty()) rule.scene_keywords.push_back(*kw.asString());
        }
        for (const auto &kw : *task_kw->asArray()) {
            if (kw.asString() && !kw.asString()->empty()) rule.task_keywords.push_back(*kw.asString());
        }
        if (rule.scene_keywords.empty() && rule.task_keywords.empty()) {
            return fail("plugin config invalid field: routing.rules[] must contain scene_keywords or task_keywords");
        }
        spec.route_config.rules.push_back(std::move(rule));
    }

    *out_spec = std::move(spec);
    if (out_error) out_error->clear();
    return true;
}

}  // namespace

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPlugin(const PluginArtifactSpec &spec) {
    if (spec.onnx_path.empty() || spec.config_path.empty()) {
        return nullptr;
    }
    return std::make_unique<OnnxBehaviorPlugin>(spec);
}

PluginWorkerConfig BuildPluginWorkerConfig(const PluginArtifactSpec::WorkerTuning &tuning) {
    PluginWorkerConfig cfg{};
    cfg.update_hz = tuning.update_hz;
    cfg.frame_budget_ms = tuning.frame_budget_ms;
    cfg.timeout_degrade_threshold = tuning.timeout_degrade_threshold;
    cfg.degrade_hz_steps = tuning.degrade_hz_steps;
    cfg.recover_after_consecutive_successes = tuning.recover_after_consecutive_successes;
    cfg.avg_latency_budget_ms = tuning.avg_latency_budget_ms;
    cfg.latency_budget_window_size = tuning.latency_budget_window_size;
    cfg.disable_after_consecutive_failures = tuning.disable_after_consecutive_failures;
    cfg.auto_recover_after_ms = tuning.auto_recover_after_ms;
    return cfg;
}

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPluginFromConfig(const std::string &config_path,
                                                                    std::string *out_error,
                                                                    PluginArtifactSpec *out_spec) {
    const std::string text = ReadTextFile(config_path);
    if (text.empty()) {
        if (out_error) *out_error = "failed to read plugin config: " + config_path;
        return nullptr;
    }

    JsonParseError parse_err{};
    auto root_opt = ParseJson(text, &parse_err);
    if (!root_opt) {
        if (out_error) *out_error = "plugin config parse failed";
        return nullptr;
    }

    PluginArtifactSpec spec{};
    if (!ValidatePluginConfig(*root_opt, config_path, &spec, out_error)) {
        return nullptr;
    }
    if (out_spec) {
        *out_spec = spec;
    }
    return CreateOnnxBehaviorPlugin(spec);
}

struct PluginWorker::Impl {
    PluginManager *manager = nullptr;
    PluginWorkerConfig cfg{};

    std::atomic<bool> running{false};
    std::thread worker;

    std::mutex mtx;
    PerceptionInput latest_in{};
    BehaviorOutput latest_out{};
    std::uint64_t out_seq = 0;
    std::uint64_t consumed_seq = 0;

    std::uint64_t total_update_count = 0;
    std::uint64_t success_count = 0;
    std::uint64_t timeout_count = 0;
    std::uint64_t exception_count = 0;
    std::uint64_t internal_error_count = 0;
    std::uint64_t disable_count = 0;
    std::uint64_t recover_count = 0;
    int current_update_hz = 60;
    int consecutive_timeout_count = 0;
    int consecutive_success_count = 0;
    int consecutive_failures = 0;
    bool auto_disabled = false;
    std::chrono::steady_clock::time_point last_disable_tp{};
    std::string last_error;

    std::string model_id = "unknown";
    std::string model_version = "0.0.0";
    std::string backend = "onnxruntime.cpu";
    int batch = 1;
    double last_latency_ms = 0.0;
    std::vector<double> latency_window_ms;

    int PickNextDegradeHz() const {
        int next_hz = current_update_hz;
        for (int hz : cfg.degrade_hz_steps) {
            if (hz < current_update_hz) {
                next_hz = hz;
                break;
            }
        }
        return std::max(1, next_hz);
    }

    int PickNextRecoverHz() const {
        int next_hz = current_update_hz;
        for (auto it = cfg.degrade_hz_steps.rbegin(); it != cfg.degrade_hz_steps.rend(); ++it) {
            if (*it > current_update_hz) {
                next_hz = *it;
                break;
            }
        }
        return std::max(1, next_hz);
    }

    double ComputeAverageLatencyMs() const {
        if (latency_window_ms.empty()) {
            return 0.0;
        }
        double sum = 0.0;
        for (double v : latency_window_ms) {
            sum += v;
        }
        return sum / static_cast<double>(latency_window_ms.size());
    }
};

PluginWorker::PluginWorker() = default;

PluginWorker::~PluginWorker() {
    Stop();
}

PluginWorker::PluginWorker(PluginWorker &&other) noexcept = default;

PluginWorker &PluginWorker::operator=(PluginWorker &&other) noexcept {
    if (this != &other) {
        Stop();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool PluginWorker::Start(PluginManager *manager, PluginWorkerConfig cfg, std::string *out_error) {
    if (!manager) {
        if (out_error) *out_error = "PluginWorker Start failed: manager is null";
        return false;
    }
    if (!manager->IsReady()) {
        if (out_error) *out_error = "PluginWorker Start failed: plugin manager not initialized";
        return false;
    }
    if (impl_ && impl_->running.load()) {
        if (out_error) *out_error = "PluginWorker Start failed: already running";
        return false;
    }

    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }

    impl_->manager = manager;
    impl_->cfg = cfg;
    impl_->cfg.update_hz = std::max(1, impl_->cfg.update_hz);
    impl_->cfg.frame_budget_ms = std::max(1, impl_->cfg.frame_budget_ms);
    impl_->cfg.timeout_degrade_threshold = std::max(1, impl_->cfg.timeout_degrade_threshold);
    impl_->cfg.disable_after_consecutive_failures = std::max(1, impl_->cfg.disable_after_consecutive_failures);
    impl_->cfg.auto_recover_after_ms = std::max(100, impl_->cfg.auto_recover_after_ms);
    impl_->cfg.recover_after_consecutive_successes = std::max(1, impl_->cfg.recover_after_consecutive_successes);
    impl_->cfg.avg_latency_budget_ms = std::max(0.1, impl_->cfg.avg_latency_budget_ms);
    impl_->cfg.latency_budget_window_size = std::max<std::size_t>(4, impl_->cfg.latency_budget_window_size);
    if (impl_->cfg.degrade_hz_steps.empty()) {
        impl_->cfg.degrade_hz_steps = {impl_->cfg.update_hz, 60, 30, 15, 10};
    }
    std::sort(impl_->cfg.degrade_hz_steps.begin(), impl_->cfg.degrade_hz_steps.end(), std::greater<int>());
    impl_->cfg.degrade_hz_steps.erase(std::remove_if(impl_->cfg.degrade_hz_steps.begin(),
                                                     impl_->cfg.degrade_hz_steps.end(),
                                                     [](int hz) { return hz <= 0; }),
                                      impl_->cfg.degrade_hz_steps.end());
    if (impl_->cfg.degrade_hz_steps.empty() || impl_->cfg.degrade_hz_steps.front() != impl_->cfg.update_hz) {
        impl_->cfg.degrade_hz_steps.insert(impl_->cfg.degrade_hz_steps.begin(), impl_->cfg.update_hz);
        std::sort(impl_->cfg.degrade_hz_steps.begin(), impl_->cfg.degrade_hz_steps.end(), std::greater<int>());
    }
    impl_->current_update_hz = impl_->cfg.update_hz;
    impl_->consecutive_timeout_count = 0;
    impl_->consecutive_success_count = 0;
    impl_->consecutive_failures = 0;
    impl_->auto_disabled = false;
    impl_->total_update_count = 0;
    impl_->success_count = 0;
    impl_->timeout_count = 0;
    impl_->exception_count = 0;
    impl_->internal_error_count = 0;
    impl_->disable_count = 0;
    impl_->recover_count = 0;
    impl_->last_latency_ms = 0.0;
    impl_->latency_window_ms.clear();
    impl_->last_error.clear();
    const PluginDescriptor &desc = manager->Descriptor();
    impl_->model_id = desc.name ? desc.name : "unknown";
    impl_->model_version = desc.version ? desc.version : "0.0.0";
    impl_->backend = "onnxruntime.cpu";
    impl_->batch = 1;
    impl_->running.store(true);

    impl_->worker = std::thread([this]() {
        while (impl_->running.load()) {
            const int current_hz = std::max(1, impl_->current_update_hz);
            const auto step = std::chrono::milliseconds(std::max(1, 1000 / current_hz));
            const auto t0 = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(impl_->mtx);
                if (impl_->auto_disabled) {
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t0 - impl_->last_disable_tp).count();
                    if (ms >= impl_->cfg.auto_recover_after_ms) {
                        impl_->auto_disabled = false;
                        impl_->consecutive_failures = 0;
                        ++impl_->recover_count;
                        impl_->last_error = "plugin auto recovered";
                    }
                }
            }

            PerceptionInput in{};
            {
                std::lock_guard<std::mutex> lock(impl_->mtx);
                in = impl_->latest_in;
            }

            BehaviorOutput out{};
            std::string err;
            PluginStatus st = PluginStatus::InternalError;
            bool caught_exception = false;
            try {
                st = impl_->manager->Update(in, out, &err);
            } catch (const std::exception &e) {
                err = std::string("PluginWorker Update exception: ") + e.what();
                st = PluginStatus::InternalError;
                caught_exception = true;
            } catch (...) {
                err = "PluginWorker Update exception: unknown";
                st = PluginStatus::InternalError;
                caught_exception = true;
            }

            const double elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count());

            {
                std::lock_guard<std::mutex> lock(impl_->mtx);
                ++impl_->total_update_count;
                impl_->last_latency_ms = elapsed_ms;
                impl_->latency_window_ms.push_back(std::max(0.0, elapsed_ms));
                if (impl_->latency_window_ms.size() > impl_->cfg.latency_budget_window_size) {
                    impl_->latency_window_ms.erase(impl_->latency_window_ms.begin());
                }
                const double avg_latency_ms = impl_->ComputeAverageLatencyMs();
                if (st == PluginStatus::Ok) {
                    impl_->latest_out = out;
                    ++impl_->out_seq;
                    ++impl_->success_count;
                    ++impl_->consecutive_success_count;
                    impl_->consecutive_timeout_count = 0;
                    impl_->consecutive_failures = 0;
                    impl_->auto_disabled = false;
                    impl_->last_error.clear();

                    if (avg_latency_ms > impl_->cfg.avg_latency_budget_ms) {
                        const int next_hz = impl_->PickNextDegradeHz();
                        if (next_hz != impl_->current_update_hz) {
                            impl_->current_update_hz = next_hz;
                            impl_->consecutive_success_count = 0;
                            SDL_Log("PluginWorker degraded update_hz to %d due to avg latency budget %.2fms exceeded (avg=%.2fms)",
                                    next_hz,
                                    impl_->cfg.avg_latency_budget_ms,
                                    avg_latency_ms);
                        }
                    } else if (impl_->consecutive_success_count >= impl_->cfg.recover_after_consecutive_successes) {
                        const int next_hz = impl_->PickNextRecoverHz();
                        if (next_hz != impl_->current_update_hz) {
                            impl_->current_update_hz = next_hz;
                            SDL_Log("PluginWorker recovered update_hz to %d after %d consecutive successes",
                                    next_hz,
                                    impl_->consecutive_success_count);
                        }
                        impl_->consecutive_success_count = 0;
                    }
                } else {
                    ++impl_->consecutive_failures;
                    impl_->consecutive_success_count = 0;
                    if (st == PluginStatus::Timeout) {
                        ++impl_->timeout_count;
                        ++impl_->consecutive_timeout_count;

                        if (impl_->consecutive_timeout_count >= impl_->cfg.timeout_degrade_threshold) {
                            const int next_hz = impl_->PickNextDegradeHz();
                            if (next_hz != impl_->current_update_hz) {
                                impl_->current_update_hz = next_hz;
                                SDL_Log("PluginWorker degraded update_hz to %d due to consecutive timeouts", next_hz);
                            }
                            impl_->consecutive_timeout_count = 0;
                        }
                    } else {
                        impl_->consecutive_timeout_count = 0;
                        ++impl_->internal_error_count;
                    }

                    if (caught_exception) {
                        ++impl_->exception_count;
                    }
                    if (!err.empty()) {
                        impl_->last_error = err;
                    }

                    if (!impl_->auto_disabled && impl_->consecutive_failures >= impl_->cfg.disable_after_consecutive_failures) {
                        impl_->auto_disabled = true;
                        impl_->last_disable_tp = std::chrono::steady_clock::now();
                        ++impl_->disable_count;
                        impl_->last_error = "plugin auto disabled due to consecutive failures";
                    }
                }
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0);
            if (elapsed.count() < impl_->cfg.frame_budget_ms) {
                std::this_thread::sleep_for(step - std::min(step, elapsed));
            }

            if (impl_->auto_disabled) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    });

    return true;
}

void PluginWorker::Stop() noexcept {
    if (!impl_) return;
    impl_->running.store(false);
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}

void PluginWorker::SubmitInput(const PerceptionInput &in) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->latest_in = in;
}

bool PluginWorker::TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq) {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->out_seq == impl_->consumed_seq) {
        return false;
    }
    out = impl_->latest_out;
    impl_->consumed_seq = impl_->out_seq;
    if (out_seq) *out_seq = impl_->out_seq;
    return true;
}

PluginWorkerStats PluginWorker::GetStats() const {
    if (!impl_) {
        return PluginWorkerStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mtx);
    PluginWorkerStats s{};
    s.total_update_count = impl_->total_update_count;
    s.success_count = impl_->success_count;
    s.timeout_count = impl_->timeout_count;
    s.exception_count = impl_->exception_count;
    s.internal_error_count = impl_->internal_error_count;
    s.disable_count = impl_->disable_count;
    s.recover_count = impl_->recover_count;
    s.current_update_hz = impl_->current_update_hz;
    s.auto_disabled = impl_->auto_disabled;
    s.model_id = impl_->model_id;
    s.model_version = impl_->model_version;
    s.backend = impl_->backend;
    s.batch = impl_->batch;
    s.last_latency_ms = impl_->last_latency_ms;
    s.avg_latency_ms = impl_->ComputeAverageLatencyMs();
    if (!impl_->latency_window_ms.empty()) {
        std::vector<double> sorted = impl_->latency_window_ms;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t last = sorted.size() - 1;
        const std::size_t p50_idx = static_cast<std::size_t>(std::floor(0.50 * static_cast<double>(last)));
        const std::size_t p95_idx = static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(last)));
        s.latency_p50_ms = sorted[std::min(p50_idx, last)];
        s.latency_p95_ms = sorted[std::min(p95_idx, last)];
    }
    s.success_rate = impl_->total_update_count > 0
                         ? static_cast<double>(impl_->success_count) / static_cast<double>(impl_->total_update_count)
                         : 0.0;
    s.timeout_rate = impl_->total_update_count > 0
                         ? static_cast<double>(impl_->timeout_count) / static_cast<double>(impl_->total_update_count)
                         : 0.0;
    s.last_error = impl_->last_error;
    return s;
}

}  // namespace k2d
