#include "desktoper2D/lifecycle/inference_adapter.h"

#include <memory>
#include <utility>

#include <algorithm>
#include <utility>
#include <vector>

namespace desktoper2D {

PluginInferenceAdapter::PluginInferenceAdapter(std::unique_ptr<IBehaviorPlugin> plugin,
                                               std::string config_path,
                                               PluginWorkerConfig worker_cfg)
    : config_path_(std::move(config_path)), worker_cfg_(std::move(worker_cfg)) {
    pending_plugin_ = std::move(plugin);
    hot_reload_enabled_ = !config_path_.empty();
}

bool PluginInferenceAdapter::Init(const PluginRuntimeConfig &runtime_cfg,
                                  const PluginHostCallbacks &host,
                                  const PluginWorkerConfig &worker_cfg,
                                  std::string *out_error) {
    runtime_cfg_ = runtime_cfg;
    host_ = host;
    if (worker_cfg.update_hz > 0) {
        worker_cfg_ = worker_cfg;
    }

    if (!BuildManager(std::move(pending_plugin_), out_error)) {
        ready_ = false;
        return false;
    }

    if (!manager_->Init(runtime_cfg, host, worker_cfg_, out_error)) {
        ready_ = false;
        return false;
    }

    if (hot_reload_enabled_) {
        std::error_code ec;
        config_last_write_time_ = std::filesystem::last_write_time(config_path_, ec);
        config_last_write_time_valid_ = !ec;
        hot_reload_last_check_tp_ = std::chrono::steady_clock::now();
    }

    ready_ = true;
    return true;
}

void PluginInferenceAdapter::Shutdown() noexcept {
    if (manager_) {
        manager_->Shutdown();
    }
    manager_.reset();
    behavior_module_ = nullptr;
    ready_ = false;
}

void PluginInferenceAdapter::SubmitInput(const PerceptionInput &in) {
    if (!ready_ || !manager_) return;
    manager_->SubmitInput(in);
}

bool PluginInferenceAdapter::TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq) {
    if (!ready_ || !manager_) return false;
    return manager_->TryConsumeLatestBehaviorOutput(out, out_seq);
}

void PluginInferenceAdapter::TickHotReload(const float dt_sec) {
    if (!ready_ || !hot_reload_enabled_) {
        return;
    }

    hot_reload_accum_sec_ += std::max(0.0f, dt_sec);
    if (hot_reload_accum_sec_ < 1.0f) {
        return;
    }
    hot_reload_accum_sec_ = 0.0f;

    TryHotReloadOnInputPath();
}

bool PluginInferenceAdapter::IsReady() const noexcept {
    return ready_;
}

PluginWorkerStats PluginInferenceAdapter::GetStats() const {
    if (!manager_) return {};
    return manager_->GetStats();
}

PluginWorkerConfig PluginInferenceAdapter::GetWorkerConfig() const {
    return worker_cfg_;
}

bool PluginInferenceAdapter::SwitchPluginByConfigPath(const std::string &config_path,
                                                      std::string *out_error) {
    std::string err;
    PluginArtifactSpec spec{};
    auto plugin = CreateOnnxBehaviorPluginFromConfig(config_path, &err, &spec);
    if (!plugin) {
        if (out_error) *out_error = err.empty() ? "plugin switch failed: invalid config" : err;
        return false;
    }

    if (!BuildManager(std::move(plugin), &err)) {
        if (out_error) *out_error = err.empty() ? "plugin switch failed: build manager error" : err;
        return false;
    }

    const PluginWorkerConfig worker_cfg = BuildPluginWorkerConfig(spec.worker_tuning);
    worker_cfg_ = worker_cfg;

    if (!manager_->Init(runtime_cfg_, host_, worker_cfg_, &err)) {
        if (out_error) *out_error = err.empty() ? "plugin switch failed: init error" : err;
        return false;
    }

    config_path_ = config_path;
    hot_reload_enabled_ = !config_path_.empty();
    if (hot_reload_enabled_) {
        std::error_code ec;
        config_last_write_time_ = std::filesystem::last_write_time(config_path_, ec);
        config_last_write_time_valid_ = !ec;
        hot_reload_last_check_tp_ = std::chrono::steady_clock::now();
    } else {
        config_last_write_time_valid_ = false;
    }

    ready_ = true;
    if (out_error) out_error->clear();
    return true;
}

const PluginDescriptor &PluginInferenceAdapter::Descriptor() const noexcept {
    if (behavior_module_) {
        return behavior_module_->Descriptor();
    }
    static PluginDescriptor default_desc{};
    return default_desc;
}

void PluginInferenceAdapter::TryHotReloadOnInputPath() {
    if (!ready_ || !hot_reload_enabled_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (hot_reload_last_check_tp_.time_since_epoch().count() != 0) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - hot_reload_last_check_tp_).count();
        if (ms < 1000) {
            return;
        }
    }
    hot_reload_last_check_tp_ = now;

    std::error_code ec;
    const auto new_write_time = std::filesystem::last_write_time(config_path_, ec);
    if (ec) {
        return;
    }
    if (config_last_write_time_valid_ && new_write_time <= config_last_write_time_) {
        return;
    }

    std::string err;
    PluginArtifactSpec shadow_spec{};
    auto shadow_plugin = CreateOnnxBehaviorPluginFromConfig(config_path_, &err, &shadow_spec);
    if (!shadow_plugin) {
        return;
    }

    PluginManager shadow_manager;
    shadow_manager.SetPlugin(std::move(shadow_plugin));
    if (shadow_manager.Init(runtime_cfg_, host_, &err) != PluginStatus::Ok) {
        shadow_manager.Destroy();
        return;
    }

    PerceptionInput dry_in{};
    BehaviorOutput dry_out{};
    if (shadow_manager.Update(dry_in, dry_out, &err) != PluginStatus::Ok) {
        shadow_manager.Destroy();
        return;
    }
    shadow_manager.Destroy();

    PluginArtifactSpec commit_spec{};
    auto commit_plugin = CreateOnnxBehaviorPluginFromConfig(config_path_, &err, &commit_spec);
    if (!commit_plugin) {
        return;
    }

    if (!BuildManager(std::move(commit_plugin), &err)) {
        return;
    }

    if (!manager_->Init(runtime_cfg_, host_, worker_cfg_, &err)) {
        return;
    }

    config_last_write_time_ = new_write_time;
    config_last_write_time_valid_ = true;
}

bool PluginInferenceAdapter::BuildManager(std::unique_ptr<IBehaviorPlugin> plugin, std::string *out_error) {
    if (manager_) {
        manager_->Shutdown();
    }
    manager_.reset();
    behavior_module_ = nullptr;

    manager_ = std::make_unique<PluginWorkerManager>();

    if (!plugin) {
        if (out_error) *out_error = "plugin adapter build failed: null plugin";
        return false;
    }

    auto behavior_module = std::make_unique<BehaviorPluginModule>(std::move(plugin), worker_cfg_);
    behavior_module_ = behavior_module.get();
    manager_->RegisterModule(std::move(behavior_module));
    return true;
}

std::unique_ptr<IInferenceAdapter> CreateDefaultInferenceAdapter(const std::vector<std::string> &config_candidates) {
    std::string plugin_err;

    for (const auto &cfg_path : config_candidates) {
        PluginArtifactSpec spec{};
        if (auto plugin = CreateOnnxBehaviorPluginFromConfig(cfg_path, &plugin_err, &spec)) {
            const PluginWorkerConfig worker_cfg = BuildPluginWorkerConfig(spec.worker_tuning);
            return std::make_unique<PluginInferenceAdapter>(std::move(plugin), cfg_path, worker_cfg);
        }
    }

    PluginWorkerConfig default_worker_cfg{};
    return std::make_unique<PluginInferenceAdapter>(CreateDefaultBehaviorPlugin(), "", default_worker_cfg);
}

}  // namespace desktoper2D
