#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"

#include <chrono>

#include "desktoper2D/lifecycle/inference_adapter.h"
#include "desktoper2D/lifecycle/resource_locator.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service_internal.h"

namespace desktoper2D {

using namespace plugin_runtime_detail;

void AppendPluginLog(AppRuntime &runtime,
                     const std::string &id,
                     PluginLogLevel level,
                     const std::string &message,
                     int error_code) {
    auto &queue = runtime.plugin.logs[id];
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    const std::int64_t ts_ms = static_cast<std::int64_t>(now.time_since_epoch().count());
    queue.push_back(PluginLogEntry{
        .ts_ms = ts_ms,
        .level = level,
        .message = message,
        .error_code = error_code,
    });
    const std::size_t kMaxLogs = 200;
    while (queue.size() > kMaxLogs) {
        queue.pop_front();
    }
}

void UpdatePluginLifecycle(AppRuntime &runtime) {
    if (runtime.plugin.config_refresh_requested) {
        RefreshPluginConfigs(runtime);
    }
    if (runtime.plugin.asr_provider_entries.empty()) {
        RefreshAsrProviders(runtime);
    }
    if (runtime.plugin.ocr_model_entries.empty()) {
        RefreshOcrModels(runtime);
    }
    if (runtime.plugin.default_plugin_catalog_refresh_requested ||
        runtime.plugin.default_plugin_catalog_entries.empty()) {
        RefreshDefaultPluginCatalog(runtime);
    }
    if (runtime.plugin.unified_refresh_requested || runtime.plugin.unified_entries.empty()) {
        RefreshUnifiedPlugins(runtime);
    }

    const bool want_enabled = runtime.feature_flags.plugin_enabled;
    const bool is_ready = runtime.plugin.ready;

    if (want_enabled) {
        if (!runtime.plugin.inference_adapter) {
            const auto plugin_config_candidates = ResourceLocator::BuildCandidatePaths(
                "assets/plugin_behavior_config.json");
            runtime.plugin.inference_adapter = CreateDefaultInferenceAdapter(plugin_config_candidates);
        }
        if (!is_ready && runtime.plugin.inference_adapter) {
            std::string err;
            const PluginRuntimeConfig cfg = BuildPluginRuntimeConfig(runtime);
            const PluginHostCallbacks host = BuildPluginHostCallbacks();
            const PluginWorkerConfig worker_cfg = runtime.plugin.inference_adapter->GetWorkerConfig();
            runtime.plugin.ready = runtime.plugin.inference_adapter->Init(cfg, host, worker_cfg, &err);
            if (!runtime.plugin.ready) {
                const std::string msg = err.empty() ? std::string("plugin init failed") : err;
                ApplyPluginInitFailure(runtime, msg);
                AppendPluginLog(runtime, "behavior:bootstrap", PluginLogLevel::Error, msg,
                                ToErrorCode(msg, RuntimeErrorCode::InitFailed));
            } else {
                ApplyPluginHealthy(runtime);
            }
        }
        return;
    }

    if (runtime.plugin.inference_adapter) {
        runtime.plugin.inference_adapter->Shutdown();
        runtime.plugin.inference_adapter.reset();
    }
    runtime.plugin.ready = false;
    runtime.plugin.last_error = "plugin disabled";
    ClearRuntimeError(runtime.plugin.error_info);
}

}  // namespace desktoper2D
