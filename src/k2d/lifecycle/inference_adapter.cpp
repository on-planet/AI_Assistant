#include "k2d/lifecycle/inference_adapter.h"

#include <utility>
#include <vector>

namespace k2d {

PluginInferenceAdapter::PluginInferenceAdapter(std::unique_ptr<IBehaviorPlugin> plugin) {
    manager_.SetPlugin(std::move(plugin));
}

bool PluginInferenceAdapter::Init(const PluginRuntimeConfig &runtime_cfg,
                                  const PluginHostCallbacks &host,
                                  const PluginWorkerConfig &worker_cfg,
                                  std::string *out_error) {
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

std::unique_ptr<IInferenceAdapter> CreateDefaultInferenceAdapter() {
    std::string plugin_err;
    const std::vector<std::string> config_candidates = {
        "assets/plugin_behavior_config.json",
        "../assets/plugin_behavior_config.json",
        "../../assets/plugin_behavior_config.json",
    };

    for (const auto &cfg_path : config_candidates) {
        if (auto plugin = CreateOnnxBehaviorPluginFromConfig(cfg_path, &plugin_err)) {
            return std::make_unique<PluginInferenceAdapter>(std::move(plugin));
        }
    }

    return std::make_unique<PluginInferenceAdapter>(CreateDefaultBehaviorPlugin());
}

}  // namespace k2d
