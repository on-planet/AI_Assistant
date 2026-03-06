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
        out.event_scores["plugin.route." + route.name] = 1.0f;

        out.param_targets["window.opacity"] = std::clamp(base_opacity_ + opacity_bias + presence * 0.05f, 0.05f, 1.0f);
        out.param_weights["window.opacity"] = 1.0f;
        out.param_targets["window.click_through"] = base_click_through_ ? 1.0f : 0.0f;
        out.param_weights["window.click_through"] = 1.0f;
        out.param_targets["runtime.show_debug_stats"] = base_show_debug_stats_ ? 1.0f : 0.0f;
        out.param_weights["runtime.show_debug_stats"] = 1.0f;
        out.param_targets["runtime.manual_param_mode"] = base_manual_param_mode_ ? 1.0f : 0.0f;
        out.param_weights["runtime.manual_param_mode"] = 1.0f;
        out.event_scores["plugin.models.count"] = static_cast<float>(1 + spec_.extra_onnx_paths.size());
        out.event_scores["plugin.route.selected"] = (route.name == route_config_.default_route ? 0.0f : 1.0f);

        const std::vector<float> mock_logits = {
            std::clamp(presence, 0.0f, 1.0f),
            std::clamp(1.0f - presence, 0.0f, 1.0f),
            std::clamp(0.25f + opacity_bias, 0.0f, 1.0f),
        };
        const PostprocessTrace trace = ExecutePostprocessPipeline(mock_logits, postprocess_);
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
    struct RouteMatch {
        std::string name = "unknown";
        float opacity_bias = 0.0f;
    };

    static std::string ToLowerCopy(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return out;
    }

    static bool ContainsAny(const std::string &text_lower, const std::vector<std::string> &keywords) {
        for (const std::string &kw : keywords) {
            if (kw.empty()) continue;
            if (text_lower.find(ToLowerCopy(kw)) != std::string::npos) return true;
        }
        return false;
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

    RouteMatch ResolveRoute(const PerceptionInput &in) const {
        const std::string scene = ToLowerCopy(in.scene_label);
        const std::string task = ToLowerCopy(in.task_label);

        for (const PluginRouteRule &rule : route_config_.rules) {
            if (rule.name.empty()) continue;
            const bool scene_hit = !rule.scene_keywords.empty() && ContainsAny(scene, rule.scene_keywords);
            const bool task_hit = !rule.task_keywords.empty() && ContainsAny(task, rule.task_keywords);
            if (scene_hit || task_hit) {
                return RouteMatch{.name = rule.name, .opacity_bias = rule.opacity_bias};
            }
        }

        return RouteMatch{.name = route_config_.default_route.empty() ? std::string("unknown") : route_config_.default_route,
                          .opacity_bias = 0.0f};
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

    spec.route_config.default_route = *default_route;
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

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPluginFromConfig(const std::string &config_path,
                                                                    std::string *out_error) {
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
    std::uint64_t timeout_count = 0;
    std::uint64_t exception_count = 0;
    std::uint64_t internal_error_count = 0;
    std::uint64_t disable_count = 0;
    std::uint64_t recover_count = 0;
    int current_update_hz = 60;
    int consecutive_timeout_count = 0;
    int consecutive_failures = 0;
    bool auto_disabled = false;
    std::chrono::steady_clock::time_point last_disable_tp{};
    std::string last_error;
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
    impl_->current_update_hz = impl_->cfg.update_hz;
    impl_->consecutive_timeout_count = 0;
    impl_->consecutive_failures = 0;
    impl_->auto_disabled = false;
    impl_->total_update_count = 0;
    impl_->timeout_count = 0;
    impl_->exception_count = 0;
    impl_->internal_error_count = 0;
    impl_->disable_count = 0;
    impl_->recover_count = 0;
    impl_->last_error.clear();
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

            {
                std::lock_guard<std::mutex> lock(impl_->mtx);
                ++impl_->total_update_count;
                if (st == PluginStatus::Ok) {
                    impl_->latest_out = out;
                    ++impl_->out_seq;
                    impl_->consecutive_timeout_count = 0;
                    impl_->consecutive_failures = 0;
                    impl_->auto_disabled = false;
                    impl_->last_error.clear();
                } else {
                    ++impl_->consecutive_failures;
                    if (st == PluginStatus::Timeout) {
                        ++impl_->timeout_count;
                        ++impl_->consecutive_timeout_count;

                        if (impl_->consecutive_timeout_count >= impl_->cfg.timeout_degrade_threshold) {
                            int next_hz = impl_->current_update_hz;
                            if (next_hz > 120) {
                                next_hz = 120;
                            } else if (next_hz > 60) {
                                next_hz = 60;
                            } else if (next_hz > 30) {
                                next_hz = 30;
                            } else if (next_hz > 15) {
                                next_hz = 15;
                            } else if (next_hz > 10) {
                                next_hz = 10;
                            }
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
    s.timeout_count = impl_->timeout_count;
    s.exception_count = impl_->exception_count;
    s.internal_error_count = impl_->internal_error_count;
    s.disable_count = impl_->disable_count;
    s.recover_count = impl_->recover_count;
    s.current_update_hz = impl_->current_update_hz;
    s.auto_disabled = impl_->auto_disabled;
    s.last_error = impl_->last_error;
    return s;
}

}  // namespace k2d
