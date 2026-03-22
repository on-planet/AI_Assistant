#pragma once

#include "desktoper2D/lifecycle/perception_pipeline.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"

namespace desktoper2D {

class OcrPluginModule : public IPluginModule {
public:
    explicit OcrPluginModule(PerceptionPipeline &pipeline, PerceptionPipelineState &state);

    const char *Name() const override;
    PluginModuleCategory Category() const override;
    [[nodiscard]] bool ProducesBehaviorOutput() const override;

    PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      std::string *out_error) override;

    PluginStatus Update(PerceptionInput in,
                        BehaviorOutput *out,
                        std::string *out_error) override;

    void Shutdown() noexcept override;

    [[nodiscard]] PluginWorkerStats GetStats() const override;
    [[nodiscard]] PluginWorkerConfig GetWorkerConfig() const override;
    [[nodiscard]] const PluginDescriptor &Descriptor() const override;

private:
    PerceptionPipeline &pipeline_;
    PerceptionPipelineState &state_;
    PluginWorkerStats stats_{};
    PerceptionInput last_input_{};
    PluginDescriptor descriptor_{};
};

} // namespace desktoper2D
