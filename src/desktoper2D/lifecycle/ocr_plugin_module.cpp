#include "desktoper2D/lifecycle/ocr_plugin_module.h"
#include "desktoper2D/lifecycle/plugin_lifecycle.h"

namespace desktoper2D {

OcrPluginModule::OcrPluginModule(PerceptionPipeline &pipeline, PerceptionPipelineState &state)
    : pipeline_(pipeline), state_(state) {
    descriptor_.name = "ocr_module";
    descriptor_.version = "0.1.0";
    descriptor_.capabilities = "ocr";
}

const char *OcrPluginModule::Name() const {
    return "ocr_module";
}

PluginModuleCategory OcrPluginModule::Category() const {
    return PluginModuleCategory::Ocr;
}

bool OcrPluginModule::ProducesBehaviorOutput() const {
    return false;
}

PluginStatus OcrPluginModule::Init(const PluginRuntimeConfig &, const PluginHostCallbacks &, std::string *out_error) {
    state_.ocr_enabled = true;
    if (out_error) {
        out_error->clear();
    }
    return PluginStatus::Ok;
}

PluginStatus OcrPluginModule::Update(PerceptionInput in,
                                     BehaviorOutput *,
                                     std::string *) {
    last_input_ = std::move(in);
    pipeline_.Tick(0.0f, state_);
    stats_.total_update_count += 1;
    stats_.success_count += 1;
    stats_.current_update_hz = 0;
    stats_.auto_disabled = false;
    stats_.module_name = Name();
    stats_.module_category = Category();
    stats_.last_latency_ms = state_.ocr_avg_latency_ms;
    stats_.avg_latency_ms = state_.ocr_avg_latency_ms;
    stats_.latency_p50_ms = state_.ocr_p95_latency_ms;
    stats_.latency_p95_ms = state_.ocr_p95_latency_ms;
    return PluginStatus::Ok;
}

void OcrPluginModule::Shutdown() noexcept {
    state_.ocr_enabled = false;
}

PluginWorkerStats OcrPluginModule::GetStats() const {
    return stats_;
}

PluginWorkerConfig OcrPluginModule::GetWorkerConfig() const {
    return {};
}

const PluginDescriptor &OcrPluginModule::Descriptor() const {
    return descriptor_;
}

}  // namespace desktoper2D
