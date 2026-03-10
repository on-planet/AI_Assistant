#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "desktoper2D/core/async_logger.h"
#include "desktoper2D/core/json.h"
#include "desktoper2D/lifecycle/plugin_backend_selector.h"
#include "desktoper2D/lifecycle/plugin_config_validator.h"
#include "desktoper2D/lifecycle/plugin_postprocess.h"
#include "desktoper2D/lifecycle/plugin_route_decision.h"

#include <onnxruntime_cxx_api.h>

#include <SDL3/SDL.h>

#if __has_include(<onnxruntime/core/providers/cuda/cuda_provider_factory.h>)
#include <onnxruntime/core/providers/cuda/cuda_provider_factory.h>
#define K2D_HAS_ORT_CUDA_FACTORY 1
#endif
#if __has_include(<onnxruntime/core/providers/dml/dml_provider_factory.h>)
#include <onnxruntime/core/providers/dml/dml_provider_factory.h>
#define K2D_HAS_ORT_DML_FACTORY 1
#endif

namespace desktoper2D {

namespace {

void SafeHostLog(const PluginHostCallbacks &host, const char *msg) {
    if (host.log) {
        host.log(host.user_data, msg ? msg : "");
        return;
    }
    SDL_Log("[Plugin] %s", msg ? msg : "");
}

std::string ReadTextFileInternal(const std::string &path) {
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

class OnnxBehaviorPlugin final : public IBehaviorPlugin {
public:
    explicit OnnxBehaviorPlugin(PluginArtifactSpec spec)
        : spec_(std::move(spec)),
          route_config_(NormalizeRouteConfig(spec_.route_config)),
          route_decision_(route_config_),
          postprocess_(spec_.postprocess) {}

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
        const std::filesystem::path ort_abs_path = std::filesystem::absolute(ort_model_path);
        const bool ort_exists = std::filesystem::exists(ort_model_path);
        const std::string ort_model_path_str = ort_model_path.string();
        const std::string ort_abs_path_str = ort_abs_path.string();
        LogInfo("onnx model path: raw=%s abs=%s exists=%d",
                ort_model_path_str.c_str(),
                ort_abs_path_str.c_str(),
                ort_exists ? 1 : 0);
        if (!ort_exists) {
            if (out_error) {
                *out_error = "Init failed: onnx file not found: " + ort_abs_path_str;
            }
            LogError("onnx model file not found: %s", ort_abs_path_str.c_str());
            return PluginStatus::InvalidArg;
        }

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

        const RouteMatch route = route_decision_.Resolve(in);
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

    PluginArtifactSpec spec_{};
    PluginRouteConfig route_config_{};
    PluginRouteDecision route_decision_;
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
};

}

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPlugin(const PluginArtifactSpec &spec) {
    if (spec.onnx_path.empty() || spec.config_path.empty()) {
        return nullptr;
    }
    return std::make_unique<OnnxBehaviorPlugin>(spec);
}

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPluginFromConfig(const std::string &config_path,
                                                                    std::string *out_error,
                                                                    PluginArtifactSpec *out_spec) {
    const std::string text = ReadTextFileInternal(config_path);
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

}  // namespace desktoper2D
