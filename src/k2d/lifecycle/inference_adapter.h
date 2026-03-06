#pragma once

#include "k2d/lifecycle/plugin_lifecycle.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace k2d {

class IInferenceAdapter {
public:
    virtual ~IInferenceAdapter() = default;

    virtual bool Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      const PluginWorkerConfig &worker_cfg,
                      std::string *out_error) = 0;

    virtual void Shutdown() noexcept = 0;

    virtual void SubmitInput(const PerceptionInput &in) = 0;
    virtual bool TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr) = 0;

    virtual bool IsReady() const noexcept = 0;
    virtual PluginWorkerStats GetStats() const = 0;
};

class PluginInferenceAdapter final : public IInferenceAdapter {
public:
    explicit PluginInferenceAdapter(std::unique_ptr<IBehaviorPlugin> plugin,
                                    std::string config_path = "");

    bool Init(const PluginRuntimeConfig &runtime_cfg,
              const PluginHostCallbacks &host,
              const PluginWorkerConfig &worker_cfg,
              std::string *out_error) override;

    void Shutdown() noexcept override;

    void SubmitInput(const PerceptionInput &in) override;
    bool TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr) override;

    bool IsReady() const noexcept override;
    PluginWorkerStats GetStats() const override;

    const PluginDescriptor &Descriptor() const noexcept;

private:
    void TryHotReloadOnInputPath();

    PluginManager manager_;
    PluginWorker worker_;
    bool ready_ = false;

    std::string config_path_;
    bool hot_reload_enabled_ = false;
    std::filesystem::file_time_type config_last_write_time_{};
    bool config_last_write_time_valid_ = false;
    std::chrono::steady_clock::time_point hot_reload_last_check_tp_{};

    PluginRuntimeConfig runtime_cfg_{};
    PluginHostCallbacks host_{};
    PluginWorkerConfig worker_cfg_{};
};

std::unique_ptr<IInferenceAdapter> CreateDefaultInferenceAdapter();

}  // namespace k2d
