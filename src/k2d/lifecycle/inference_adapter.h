#pragma once

#include "k2d/lifecycle/plugin_lifecycle.h"

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
    explicit PluginInferenceAdapter(std::unique_ptr<IBehaviorPlugin> plugin);

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
    PluginManager manager_;
    PluginWorker worker_;
    bool ready_ = false;
};

std::unique_ptr<IInferenceAdapter> CreateDefaultInferenceAdapter();

}  // namespace k2d
