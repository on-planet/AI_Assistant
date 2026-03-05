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

#include "k2d/core/json.h"

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

class OnnxBehaviorPlugin final : public IBehaviorPlugin {
public:
    explicit OnnxBehaviorPlugin(PluginArtifactSpec spec)
        : spec_(std::move(spec)), route_config_(BuildRouteConfig(spec_)) {}

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
        initialized_ = true;
        base_show_debug_stats_ = runtime_cfg.show_debug_stats;
        base_manual_param_mode_ = runtime_cfg.manual_param_mode;
        base_click_through_ = runtime_cfg.click_through;
        base_opacity_ = std::clamp(runtime_cfg.window_opacity, 0.05f, 1.0f);

        *out_desc = PluginDescriptor{
            .name = "onnx_behavior",
            .version = "0.1.0",
            .capabilities = "onnx+config,multi-model-coop",
        };
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
        out.trigger_blink = (presence > 0.8f);
        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
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
};

std::string ReadTextFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
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
    spec.config_path = config_path;
    if (const JsonValue *onnx = root_opt->get("onnx"); onnx && onnx->isString()) {
        spec.onnx_path = *onnx->asString();
    }
    if (const JsonValue *arr = root_opt->get("extra_onnx"); arr && arr->isArray()) {
        for (const auto &v : *arr->asArray()) {
            if (v.isString()) spec.extra_onnx_paths.push_back(*v.asString());
        }
    }

    if (const JsonValue *routing = root_opt->get("routing"); routing && routing->isObject()) {
        if (auto dr = routing->getString("default_route"); dr.has_value() && !dr->empty()) {
            spec.route_config.default_route = *dr;
        }

        if (const JsonValue *rules = routing->get("rules"); rules && rules->isArray() && rules->asArray()) {
            for (const auto &rv : *rules->asArray()) {
                if (!rv.isObject()) continue;

                PluginRouteRule rule{};
                if (auto name = rv.getString("name"); name.has_value()) rule.name = *name;
                rule.opacity_bias = static_cast<float>(rv.getNumber("opacity_bias").value_or(0.0));

                if (const JsonValue *scene_kw = rv.get("scene_keywords"); scene_kw && scene_kw->isArray() && scene_kw->asArray()) {
                    for (const auto &kw : *scene_kw->asArray()) {
                        if (kw.isString() && kw.asString()) rule.scene_keywords.push_back(*kw.asString());
                    }
                }
                if (const JsonValue *task_kw = rv.get("task_keywords"); task_kw && task_kw->isArray() && task_kw->asArray()) {
                    for (const auto &kw : *task_kw->asArray()) {
                        if (kw.isString() && kw.asString()) rule.task_keywords.push_back(*kw.asString());
                    }
                }

                if (!rule.name.empty()) {
                    spec.route_config.rules.push_back(std::move(rule));
                }
            }
        }
    }

    if (spec.onnx_path.empty()) {
        if (out_error) *out_error = "plugin config missing field: onnx";
        return nullptr;
    }
    if (out_error) out_error->clear();
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
