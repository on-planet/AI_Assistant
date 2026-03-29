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
        input_tensor_shape_[0] = std::max<std::int64_t>(1, spec_.tensor_schema.batch);
        input_tensor_shape_[1] = std::max<std::int64_t>(1, spec_.tensor_schema.feature_dim);
        input_tensor_values_.assign(static_cast<std::size_t>(input_tensor_shape_[1]), 0.0f);
        main_logits_scratch_.clear();
        fused_logits_scratch_.clear();
        blended_expert_logits_scratch_.clear();
        active_expert_indices_scratch_.clear();
        active_expert_indices_scratch_.reserve(spec_.expert_models.size());

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

        out.ClearPreservingCapacity();
        out.param_targets.reserve(4);
        out.param_weights.reserve(4);
        const float presence = std::clamp(in.vision.user_presence, 0.0f, 1.0f);

        const RouteMatch route = route_decision_.Resolve(in);
        const float opacity_bias = route.opacity_bias;

        const std::size_t expected_features = static_cast<std::size_t>(
            std::max<std::int64_t>(1, spec_.tensor_schema.feature_dim));
        if (input_tensor_values_.size() != expected_features) {
            input_tensor_values_.assign(expected_features, 0.0f);
        } else {
            std::fill(input_tensor_values_.begin(), input_tensor_values_.end(), 0.0f);
        }
        if (expected_features > 0) input_tensor_values_[0] = static_cast<float>(in.audio.level);
        if (expected_features > 1) input_tensor_values_[1] = static_cast<float>(in.audio.pitch_hz);
        if (expected_features > 2) input_tensor_values_[2] = static_cast<float>(in.audio.vad_prob);
        if (expected_features > 3) input_tensor_values_[3] = static_cast<float>(in.vision.user_presence);
        if (expected_features > 4) input_tensor_values_[4] = static_cast<float>(in.vision.head_yaw_deg);
        if (expected_features > 5) input_tensor_values_[5] = static_cast<float>(in.vision.head_pitch_deg);
        if (expected_features > 6) input_tensor_values_[6] = static_cast<float>(in.vision.head_roll_deg);
        if (expected_features > 7) input_tensor_values_[7] = static_cast<float>(in.state.window_visible ? 1.0f : 0.0f);

        auto run_session = [&](Ort::Session &sess, std::vector<float> &out_values, std::string *run_err) -> bool {
            try {
                if (input_tensor_values_.size() != expected_features) {
                    if (run_err) {
                        *run_err = "input feature mismatch: expected=" + std::to_string(expected_features) +
                                   " actual=" + std::to_string(input_tensor_values_.size());
                    }
                    return false;
                }

                Ort::MemoryInfo mem_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                Ort::Value input_tensor = Ort::Value::CreateTensor<float>(mem_info,
                                                                          input_tensor_values_.data(),
                                                                          input_tensor_values_.size(),
                                                                          input_tensor_shape_.data(),
                                                                          input_tensor_shape_.size());
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
                out_values.resize(elem_count);
                std::copy(data, data + elem_count, out_values.begin());
                return true;
            } catch (const std::exception &e) {
                if (run_err) *run_err = e.what();
                return false;
            }
        };

        std::string ort_err;
        if (session_ && !run_session(*session_, main_logits_scratch_, &ort_err)) {
            if (out_error) *out_error = std::string("ORT Run failed: ") + ort_err;
            return PluginStatus::InternalError;
        }

        fused_logits_scratch_ = main_logits_scratch_;
        int active_expert_count = 0;
        active_expert_indices_scratch_.clear();
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
            if (!run_session(*expert.session, expert.logits_scratch, nullptr)) {
                continue;
            }
            if (expert.logits_scratch.size() != fused_logits_scratch_.size()) {
                continue;
            }

            const float route_alignment = expert.route_name.empty() ? 0.0f : 1.0f;
            const float gating_score = route.total_score + structured_confidence + route_alignment;
            expert.last_gating_score = gating_score;
            active_expert_indices_scratch_.push_back(expert_idx);
        }

        if (!active_expert_indices_scratch_.empty()) {
            float max_logit =
                expert_sessions_[active_expert_indices_scratch_.front()].last_gating_score / gating_temperature;
            for (const std::size_t expert_idx : active_expert_indices_scratch_) {
                max_logit = std::max(max_logit, expert_sessions_[expert_idx].last_gating_score / gating_temperature);
            }

            float sum_exp = 0.0f;
            for (const std::size_t expert_idx : active_expert_indices_scratch_) {
                auto &expert = expert_sessions_[expert_idx];
                expert.last_softmax_weight = std::exp((expert.last_gating_score / gating_temperature) - max_logit);
                sum_exp += expert.last_softmax_weight;
            }

            if (sum_exp > 0.0f) {
                if (blended_expert_logits_scratch_.size() != fused_logits_scratch_.size()) {
                    blended_expert_logits_scratch_.assign(fused_logits_scratch_.size(), 0.0f);
                } else {
                    std::fill(blended_expert_logits_scratch_.begin(), blended_expert_logits_scratch_.end(), 0.0f);
                }

                for (const std::size_t expert_idx : active_expert_indices_scratch_) {
                    auto &expert = expert_sessions_[expert_idx];
                    expert.last_softmax_weight /= sum_exp;
                    for (std::size_t j = 0; j < blended_expert_logits_scratch_.size(); ++j) {
                        blended_expert_logits_scratch_[j] += expert.logits_scratch[j] * expert.last_softmax_weight;
                    }
                }

                float expert_mix_ratio = 0.0f;
                for (const std::size_t expert_idx : active_expert_indices_scratch_) {
                    const auto &expert = expert_sessions_[expert_idx];
                    expert_mix_ratio += expert.last_softmax_weight * std::clamp(expert.fusion_weight, 0.0f, 1.0f);
                }
                expert_mix_ratio = std::clamp(expert_mix_ratio, 0.0f, 1.0f);

                for (std::size_t j = 0; j < fused_logits_scratch_.size(); ++j) {
                    fused_logits_scratch_[j] = fused_logits_scratch_[j] * (1.0f - expert_mix_ratio) +
                                               blended_expert_logits_scratch_[j] * expert_mix_ratio;
                }
                active_expert_count = static_cast<int>(active_expert_indices_scratch_.size());
            }
        }
        out.route_trace.valid = true;
        out.route_trace.selected_route = route.name;
        out.route_trace.selected_non_default = (route.name != route_config_.default_route);
        out.route_trace.scene_score = route.scene_score;
        out.route_trace.task_score = route.task_score;
        out.route_trace.structured_score = route.structured_score;
        out.route_trace.presence_score = route.presence_score;
        out.route_trace.total_score = route.total_score;
        out.route_trace.active_expert_count = active_expert_count;
        out.route_trace.gating_temperature = gating_temperature;
        out.route_trace.rejected_count = std::min(route.rejected.size(), out.route_trace.rejected.size());
        for (std::size_t i = 0; i < out.route_trace.rejected_count; ++i) {
            const auto &cand = route.rejected[i];
            auto &trace = out.route_trace.rejected[i];
            trace.name = cand.name;
            trace.scene_score = cand.scene_score;
            trace.task_score = cand.task_score;
            trace.structured_score = cand.structured_score;
            trace.presence_score = cand.presence_score;
            trace.total_score = cand.total_score;
        }
        if (base_show_debug_stats_) {
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

        if (fused_logits_scratch_.empty()) {
            fused_logits_scratch_ = {
                std::clamp(presence, 0.0f, 1.0f),
                std::clamp(1.0f - presence, 0.0f, 1.0f),
                std::clamp(0.25f + opacity_bias, 0.0f, 1.0f),
            };
        }
        const PostprocessTrace trace = ExecutePostprocessPipeline(fused_logits_scratch_, postprocess_);
        out.route_trace.postprocess_argmax_index = trace.argmax_index;
        out.route_trace.postprocess_argmax_value = trace.argmax_value;
        out.route_trace.postprocess_has_threshold = trace.has_threshold;
        out.route_trace.postprocess_threshold_pass = trace.threshold_pass;

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
        std::vector<float> logits_scratch;
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
    std::array<int64_t, 2> input_tensor_shape_ = {1, 8};
    std::vector<float> input_tensor_values_;
    std::vector<float> main_logits_scratch_;
    std::vector<float> fused_logits_scratch_;
    std::vector<float> blended_expert_logits_scratch_;
    std::vector<std::size_t> active_expert_indices_scratch_;
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
