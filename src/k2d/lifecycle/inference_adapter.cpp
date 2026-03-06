#include "k2d/lifecycle/inference_adapter.h"

#include <utility>
#include <vector>

namespace k2d {

PluginInferenceAdapter::PluginInferenceAdapter(std::unique_ptr<IBehaviorPlugin> plugin,
                                               std::string config_path)
    : config_path_(std::move(config_path)) {
    manager_.SetPlugin(std::move(plugin));
    hot_reload_enabled_ = !config_path_.empty();
}

bool PluginInferenceAdapter::Init(const PluginRuntimeConfig &runtime_cfg,
                                  const PluginHostCallbacks &host,
                                  const PluginWorkerConfig &worker_cfg,
                                  std::string *out_error) {
    runtime_cfg_ = runtime_cfg;
    host_ = host;
    worker_cfg_ = worker_cfg;

    const PluginStatus st = manager_.Init(runtime_cfg, host, out_error);
    if (st != PluginStatus::Ok) {
        ready_ = false;
        return false;
    }

    if (!worker_.Start(&manager_, worker_cfg, out_error)) {
        manager_.Destroy();
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
    worker_.Stop();
    manager_.Destroy();
    ready_ = false;
}

void PluginInferenceAdapter::SubmitInput(const PerceptionInput &in) {
    if (!ready_) return;
    TryHotReloadOnInputPath();
    worker_.SubmitInput(in);
}

bool PluginInferenceAdapter::TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq) {
    if (!ready_) return false;
    return worker_.TryConsumeLatestOutput(out, out_seq);
}

bool PluginInferenceAdapter::IsReady() const noexcept {
    return ready_;
}

PluginWorkerStats PluginInferenceAdapter::GetStats() const {
    return worker_.GetStats();
}

const PluginDescriptor &PluginInferenceAdapter::Descriptor() const noexcept {
    return manager_.Descriptor();
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
    auto shadow_plugin = CreateOnnxBehaviorPluginFromConfig(config_path_, &err);
    if (!shadow_plugin) {
        return;
    }

    PluginManager shadow_manager;
    shadow_manager.SetPlugin(std::move(shadow_plugin));
    if (shadow_manager.Init(runtime_cfg_, host_, &err) != PluginStatus::Ok) {
        shadow_manager.Destroy();
        return;
    }

    // dry-run once
    PerceptionInput dry_in{};
    BehaviorOutput dry_out{};
    if (shadow_manager.Update(dry_in, dry_out, &err) != PluginStatus::Ok) {
        shadow_manager.Destroy();
        return;
    }
    shadow_manager.Destroy();

    // commit switch
    worker_.Stop();
    manager_.Destroy();

    auto commit_plugin = CreateOnnxBehaviorPluginFromConfig(config_path_, &err);
    if (!commit_plugin) {
        return;
    }
    manager_.SetPlugin(std::move(commit_plugin));
    if (manager_.Init(runtime_cfg_, host_, &err) != PluginStatus::Ok) {
        manager_.Destroy();
        return;
    }
    if (!worker_.Start(&manager_, worker_cfg_, &err)) {
        manager_.Destroy();
        return;
    }

    config_last_write_time_ = new_write_time;
    config_last_write_time_valid_ = true;
}

std::unique_ptr<IInferenceAdapter> CreateDefaultInferenceAdapter() {
    std::string plugin_err;
    const std::vector<std::string> config_candidates = {
        "assets/plugin_behavior_config.json",
        "../assets/plugin_behavior_config.json",
        "../../assets/plugin_behavior_config.json",
    };

    for (const auto &cfg_path : config_candidates) {
        if (auto plugin = CreateOnnxBehaviorPluginFromConfig(cfg_path, &plugin_err)) {
            return std::make_unique<PluginInferenceAdapter>(std::move(plugin), cfg_path);
        }
    }

    return std::make_unique<PluginInferenceAdapter>(CreateDefaultBehaviorPlugin());
}

}  // namespace k2d
