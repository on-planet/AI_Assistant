#include "desktoper2D/lifecycle/services/plugin_runtime_switch_service.h"

#include <algorithm>
#include <memory>

#include "desktoper2D/lifecycle/asr/cloud_asr_provider.h"
#include "desktoper2D/lifecycle/asr/hybrid_asr_provider.h"
#include "desktoper2D/lifecycle/asr/offline_asr_provider.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service_internal.h"

namespace desktoper2D {

using namespace plugin_runtime_detail;

bool SwitchPluginByName(AppRuntime &runtime, const std::string &name, std::string *out_error) {
    if (!runtime.plugin.inference_adapter) {
        if (out_error) *out_error = "plugin adapter not initialized";
        return false;
    }
    const auto it = std::find_if(runtime.plugin.config_entries.begin(), runtime.plugin.config_entries.end(),
                                 [&](const PluginConfigEntry &entry) { return entry.name == name; });
    if (it == runtime.plugin.config_entries.end()) {
        if (out_error) *out_error = "plugin not found: " + name;
        return false;
    }
    std::string err;
    const bool ok = runtime.plugin.inference_adapter->SwitchPluginByConfigPath(it->config_path, &err);
    if (!ok) {
        const std::string msg = err.empty() ? "plugin switch failed" : err;
        if (out_error) *out_error = msg;
        AppendPluginLog(runtime, "behavior:" + it->config_path, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    }
    AppendPluginLog(runtime, "behavior:" + it->config_path, PluginLogLevel::Info, "switch ok");
    if (out_error) out_error->clear();
    return true;
}

bool ReloadPluginByConfigPath(AppRuntime &runtime, const std::string &config_path, std::string *out_error) {
    if (!runtime.plugin.inference_adapter) {
        if (out_error) *out_error = "plugin adapter not initialized";
        return false;
    }
    std::string err;
    const bool ok = runtime.plugin.inference_adapter->SwitchPluginByConfigPath(config_path, &err);
    if (!ok) {
        const std::string msg = err.empty() ? "plugin reload failed" : err;
        if (out_error) *out_error = msg;
        AppendPluginLog(runtime, "behavior:" + config_path, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    }
    AppendPluginLog(runtime, "behavior:" + config_path, PluginLogLevel::Info, "reload ok");
    if (out_error) out_error->clear();
    return true;
}

bool SwitchAsrProviderByName(AppRuntime &runtime, const std::string &name, std::string *out_error) {
    auto it = std::find_if(runtime.plugin.asr_provider_entries.begin(), runtime.plugin.asr_provider_entries.end(),
                           [&](const AsrProviderEntry &entry) { return entry.name == name; });
    if (it == runtime.plugin.asr_provider_entries.end()) {
        if (out_error) *out_error = "asr provider not found: " + name;
        return false;
    }

    std::unique_ptr<IAsrProvider> provider;
    if (it->name == "offline") {
        provider = std::make_unique<OfflineAsrProvider>(it->model_path);
    } else if (it->name == "cloud") {
        provider = std::make_unique<CloudAsrProvider>(it->endpoint, it->api_key);
    } else {
        std::unique_ptr<IAsrProvider> offline = std::make_unique<OfflineAsrProvider>(it->model_path);
        std::unique_ptr<IAsrProvider> cloud = std::make_unique<CloudAsrProvider>(it->endpoint, it->api_key);
        HybridAsrConfig asr_cfg{};
        asr_cfg.cloud_fallback_enabled = false;
        provider = std::make_unique<HybridAsrProvider>(std::move(offline), std::move(cloud), asr_cfg);
    }

    std::string err;
    const bool ok = provider && provider->Init(&err);
    if (!ok) {
        const std::string msg = err.empty() ? "asr init failed" : err;
        if (out_error) *out_error = msg;
        AppendPluginLog(runtime, "asr:" + it->name, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    }

    {
        std::lock_guard<std::mutex> provider_lock(runtime.asr_provider_mutex);
        if (runtime.asr_provider) {
            runtime.asr_provider->Shutdown();
        }
        runtime.asr_provider = std::move(provider);
        runtime.asr_provider_generation += 1;
    }
    {
        std::lock_guard<std::mutex> async_lock(runtime.asr_async_state.mutex);
        runtime.asr_async_state.request_queue.clear();
        runtime.asr_async_state.completed_queue.clear();
    }
    runtime.asr_async_state.cv.notify_all();
    runtime.asr_ready = true;
    runtime.asr_last_error.clear();
    ClearRuntimeError(runtime.asr_error_info);
    runtime.RuntimeAsrChatState::panel_state_version += 1;
    AppendPluginLog(runtime, "asr:" + it->name, PluginLogLevel::Info, "asr switch ok");
    if (out_error) out_error->clear();
    return true;
}

bool SwitchOcrModelByName(AppRuntime &runtime, const std::string &name, std::string *out_error) {
    auto it = std::find_if(runtime.plugin.ocr_model_entries.begin(), runtime.plugin.ocr_model_entries.end(),
                           [&](const OcrModelEntry &entry) { return entry.name == name; });
    if (it == runtime.plugin.ocr_model_entries.end()) {
        if (out_error) *out_error = "ocr model not found: " + name;
        return false;
    }

    std::string perception_err;
    const auto ocr_candidates = BuildOcrCandidateTriplesForPaths(it->det_path, it->rec_path, it->keys_path);
    const bool ok = runtime.perception_pipeline.ReloadOcrService(runtime.perception_state,
                                                                 ocr_candidates,
                                                                 &perception_err);
    if (!ok) {
        const std::string msg = perception_err.empty() ? "ocr init failed" : perception_err;
        if (out_error) *out_error = msg;
        AppendPluginLog(runtime, "ocr:" + it->name, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    }
    AppendPluginLog(runtime, "ocr:" + it->name, PluginLogLevel::Info, "ocr switch ok");
    if (out_error) out_error->clear();
    return true;
}

bool SwitchUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error) {
    const auto it = std::find_if(runtime.plugin.unified_entries.begin(), runtime.plugin.unified_entries.end(),
                                 [&](const UnifiedPluginEntry &entry) { return entry.id == id; });
    if (it == runtime.plugin.unified_entries.end()) {
        if (out_error) *out_error = "unified plugin not found: " + id;
        return false;
    }

    std::string err;
    bool ok = false;
    if (it->kind == UnifiedPluginKind::Asr) {
        ok = SwitchAsrProviderByName(runtime, it->name, &err);
    } else if (it->kind == UnifiedPluginKind::Ocr) {
        ok = SwitchOcrModelByName(runtime, it->name, &err);
    } else if (it->kind == UnifiedPluginKind::BehaviorUser) {
        ok = SwitchPluginByName(runtime, it->name, &err);
    } else {
        err = "switch not supported for kind: " + ToString(it->kind);
    }

    if (!ok) {
        if (out_error) *out_error = err.empty() ? "unified plugin switch failed" : err;
        const std::string msg = out_error ? *out_error : std::string("unified plugin switch failed");
        AppendPluginLog(runtime, it->id, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    }
    AppendPluginLog(runtime, it->id, PluginLogLevel::Info, "switch ok");
    if (out_error) out_error->clear();
    return true;
}

}  // namespace desktoper2D
