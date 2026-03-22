#pragma once

#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace desktoper2D {

class IInferenceAdapter {
public:
    virtual ~IInferenceAdapter() = default;

    virtual bool Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      const PluginWorkerConfig &worker_cfg,
                      std::string *out_error) = 0;

    virtual void Shutdown() noexcept = 0;

    virtual void SubmitInput(PerceptionInput in) = 0;
    virtual bool TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr) = 0;

    virtual void TickHotReload(float dt_sec) = 0;

    virtual bool IsReady() const noexcept = 0;
    virtual PluginWorkerStats GetStats() const = 0;
    virtual PluginWorkerConfig GetWorkerConfig() const = 0;

    virtual bool SwitchPluginByConfigPath(const std::string &config_path,
                                          std::string *out_error) = 0;
};

class PluginInferenceAdapter final : public IInferenceAdapter {
public:
    explicit PluginInferenceAdapter(std::unique_ptr<IBehaviorPlugin> plugin,
                                    std::string config_path = "",
                                    PluginWorkerConfig worker_cfg = {});

    bool Init(const PluginRuntimeConfig &runtime_cfg,
              const PluginHostCallbacks &host,
              const PluginWorkerConfig &worker_cfg,
              std::string *out_error) override;

    void Shutdown() noexcept override;

    void SubmitInput(PerceptionInput in) override;
    bool TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr) override;

    void TickHotReload(float dt_sec) override;

    bool IsReady() const noexcept override;
    PluginWorkerStats GetStats() const override;
    PluginWorkerConfig GetWorkerConfig() const override;

    bool SwitchPluginByConfigPath(const std::string &config_path,
                                  std::string *out_error) override;

    const PluginDescriptor &Descriptor() const noexcept;

private:
    void TryHotReloadOnInputPath();
    bool BuildManager(std::unique_ptr<IBehaviorPlugin> plugin, std::string *out_error);

    std::unique_ptr<PluginWorkerManager> manager_;
    BehaviorPluginModule *behavior_module_ = nullptr;
    bool ready_ = false;

    std::string config_path_;
    bool hot_reload_enabled_ = false;
    std::filesystem::file_time_type config_last_write_time_{};
    bool config_last_write_time_valid_ = false;
    std::chrono::steady_clock::time_point hot_reload_last_check_tp_{};
    float hot_reload_accum_sec_ = 0.0f;

    PluginRuntimeConfig runtime_cfg_{};
    PluginHostCallbacks host_{};
    PluginWorkerConfig worker_cfg_{};
    std::unique_ptr<IBehaviorPlugin> pending_plugin_;
};

std::unique_ptr<IInferenceAdapter> CreateDefaultInferenceAdapter(const std::vector<std::string> &config_candidates);

}  // namespace desktoper2D
