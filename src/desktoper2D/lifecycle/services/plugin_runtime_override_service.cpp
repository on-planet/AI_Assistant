#include "desktoper2D/lifecycle/services/plugin_runtime_override_service.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "desktoper2D/lifecycle/resource_locator.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service_internal.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_switch_service.h"

namespace desktoper2D {

using namespace plugin_runtime_detail;

namespace {

std::vector<std::pair<std::string, std::string>> BuildSceneCandidates(const AppRuntime &runtime) {
    return ResourceLocator::BuildCandidatePairs(
        runtime.plugin.override_scene_model_path.empty() ? "assets/mobileclip_image.onnx"
                                                         : runtime.plugin.override_scene_model_path,
        runtime.plugin.override_scene_labels_path.empty() ? "assets/mobileclip_labels.json"
                                                          : runtime.plugin.override_scene_labels_path);
}

std::vector<std::tuple<std::string, std::string, std::string>> BuildOcrCandidates(const AppRuntime &runtime) {
    if (!runtime.plugin.override_ocr_det_path.empty() &&
        !runtime.plugin.override_ocr_rec_path.empty() &&
        !runtime.plugin.override_ocr_keys_path.empty()) {
        return BuildOcrCandidateTriplesForPaths(runtime.plugin.override_ocr_det_path,
                                                runtime.plugin.override_ocr_rec_path,
                                                runtime.plugin.override_ocr_keys_path);
    }
    if (!runtime.plugin.ocr_model_entries.empty()) {
        const auto &entry = runtime.plugin.ocr_model_entries.front();
        return BuildOcrCandidateTriplesForPaths(entry.det_path, entry.rec_path, entry.keys_path);
    }
    return {};
}

std::vector<std::pair<std::string, std::string>> BuildFacemeshCandidates(const AppRuntime &runtime) {
    return ResourceLocator::BuildCandidatePairs(
        runtime.plugin.override_facemesh_model_path.empty() ? "assets/facemesh.onnx"
                                                            : runtime.plugin.override_facemesh_model_path,
        runtime.plugin.override_facemesh_labels_path.empty() ? "assets/facemesh.labels.json"
                                                             : runtime.plugin.override_facemesh_labels_path);
}

bool ApplyAsrOverride(AppRuntime &runtime, std::string *out_error) {
    const std::string asr_model = runtime.plugin.override_asr_model_path;
    if (asr_model.empty()) {
        if (out_error) {
            out_error->clear();
        }
        return true;
    }

    for (auto &entry : runtime.plugin.asr_provider_entries) {
        if (entry.name == "offline" || entry.name == "hybrid") {
            entry.model_path = asr_model;
        }
    }

    std::string err;
    if (!runtime.plugin.asr_current_provider_name.empty() &&
        !SwitchAsrProviderByName(runtime, runtime.plugin.asr_current_provider_name, &err)) {
        if (out_error) {
            *out_error = err;
        }
        AppendPluginLog(runtime, "asr:" + runtime.plugin.asr_current_provider_name, PluginLogLevel::Error, err,
                        ToErrorCode(err, RuntimeErrorCode::InitFailed));
        return false;
    }

    if (out_error) {
        out_error->clear();
    }
    return true;
}

}  // namespace

bool ReplaceUnifiedPluginAssets(AppRuntime &runtime,
                                const std::string &id,
                                const PluginAssetOverride &override,
                                std::string *out_error) {
    const auto it = std::find_if(runtime.plugin.unified_entries.begin(), runtime.plugin.unified_entries.end(),
                                 [&](const UnifiedPluginEntry &entry) { return entry.id == id; });
    if (it == runtime.plugin.unified_entries.end()) {
        if (out_error) *out_error = "unified plugin not found: " + id;
        return false;
    }

    std::string err;
    bool ok = false;
    if (it->kind == UnifiedPluginKind::SceneClassifier) {
        runtime.plugin.override_scene_model_path = override.onnx;
        runtime.plugin.override_scene_labels_path = override.labels;
        ok = ApplyOverrideModels(runtime, PerceptionReloadTarget::Scene, &err);
    } else if (it->kind == UnifiedPluginKind::Facemesh) {
        runtime.plugin.override_facemesh_model_path = override.onnx;
        runtime.plugin.override_facemesh_labels_path = override.labels;
        ok = ApplyOverrideModels(runtime, PerceptionReloadTarget::Facemesh, &err);
    } else if (it->kind == UnifiedPluginKind::Ocr) {
        runtime.plugin.override_ocr_det_path = override.onnx;
        runtime.plugin.override_ocr_rec_path = override.labels;
        runtime.plugin.override_ocr_keys_path = override.keys;
        ok = ApplyOverrideModels(runtime, PerceptionReloadTarget::Ocr, &err);
    } else if (it->kind == UnifiedPluginKind::Asr) {
        runtime.plugin.override_asr_model_path = override.onnx;
        ok = ApplyOverrideModels(runtime, PerceptionReloadTarget::Asr, &err);
    } else if (it->kind == UnifiedPluginKind::BehaviorUser) {
        const std::filesystem::path config_path = it->source.empty() ? it->name : it->source;
        std::string tpl = ReadTextFile(config_path.generic_string());
        if (tpl.empty()) {
            err = "failed to read plugin config: " + config_path.generic_string();
        } else {
            if (!override.onnx.empty()) {
                ReplaceJsonStringValue(tpl, "onnx", override.onnx);
            }
            if (!override.extra_onnx.empty()) {
                ReplaceJsonStringArray(tpl, "extra_onnx", override.extra_onnx);
            }
            std::ofstream ofs(config_path, std::ios::binary);
            if (!ofs) {
                err = "write plugin config failed: " + config_path.string();
            } else {
                ofs << tpl;
                ofs.close();
                ok = SwitchUnifiedPluginById(runtime, id, &err);
            }
        }
    } else {
        err = "override not supported for kind: " + ToString(it->kind);
    }

    if (!ok) {
        if (out_error) *out_error = err.empty() ? "replace unified plugin assets failed" : err;
        const std::string msg = out_error ? *out_error : std::string("replace unified plugin assets failed");
        AppendPluginLog(runtime, it->id, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InvalidConfig));
        return false;
    }
    AppendPluginLog(runtime, it->id, PluginLogLevel::Info, "assets replaced");
    if (out_error) out_error->clear();
    return true;
}

bool ApplyOverrideModels(AppRuntime &runtime, std::string *out_error) {
    return ApplyOverrideModels(runtime, PerceptionReloadTarget::All, out_error);
}

bool ApplyOverrideModels(AppRuntime &runtime,
                         PerceptionReloadTarget target,
                         std::string *out_error) {
    auto fail = [&](const std::string &msg, const std::string &log_id) {
        if (out_error) {
            *out_error = msg;
        }
        AppendPluginLog(runtime, log_id, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    };

    if ((target == PerceptionReloadTarget::All || target == PerceptionReloadTarget::Asr) &&
        !ApplyAsrOverride(runtime, out_error)) {
        const std::string msg = out_error && !out_error->empty() ? *out_error : std::string("asr override failed");
        return fail(msg, "asr:override");
    }

    if (target == PerceptionReloadTarget::All || target == PerceptionReloadTarget::Scene) {
        std::string err;
        if (!runtime.perception_pipeline.ReloadSceneClassifier(runtime.perception_state,
                                                               BuildSceneCandidates(runtime),
                                                               &err)) {
            return fail(err.empty() ? "scene override failed" : err, "scene:builtin");
        }
        AppendPluginLog(runtime, "scene:builtin", PluginLogLevel::Info, "override applied");
    }

    if (target == PerceptionReloadTarget::All || target == PerceptionReloadTarget::Ocr) {
        std::string err;
        if (!runtime.perception_pipeline.ReloadOcrService(runtime.perception_state,
                                                          BuildOcrCandidates(runtime),
                                                          &err)) {
            return fail(err.empty() ? "ocr override failed" : err, "ocr:override");
        }
        AppendPluginLog(runtime, "ocr:override", PluginLogLevel::Info, "override applied");
    }

    if (target == PerceptionReloadTarget::All || target == PerceptionReloadTarget::Facemesh) {
        std::string err;
        if (!runtime.perception_pipeline.ReloadFacemeshService(runtime.perception_state,
                                                               BuildFacemeshCandidates(runtime),
                                                               &err)) {
            return fail(err.empty() ? "facemesh override failed" : err, "facemesh:builtin");
        }
        AppendPluginLog(runtime, "facemesh:builtin", PluginLogLevel::Info, "override applied");
    }

    if (out_error) {
        out_error->clear();
    }
    return true;
}

}  // namespace desktoper2D
