#include "desktoper2D/lifecycle/services/plugin_runtime_catalog_service.h"

#include <algorithm>
#include <filesystem>

#include "desktoper2D/lifecycle/resource_locator.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_config_repository.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service_internal.h"

namespace desktoper2D {

using namespace plugin_runtime_detail;

namespace {

bool LoadDefaultPluginCatalogEntry(const std::string &path, DefaultPluginCatalogEntry *out_entry) {
    if (out_entry == nullptr) {
        return false;
    }

    const std::string text = ReadTextFile(path);
    if (text.empty()) {
        return false;
    }

    JsonParseError err{};
    auto root_opt = ParseJson(text, &err);
    if (!root_opt || !root_opt->isObject()) {
        return false;
    }

    const JsonValue &root = *root_opt;
    out_entry->name = root.getString("name").value_or(std::string());
    out_entry->kind = root.getString("kind").value_or(std::string());
    out_entry->onnx = root.getString("onnx").value_or(std::string());
    out_entry->labels = root.getString("labels").value_or(std::string());
    out_entry->keys = root.getString("keys").value_or(std::string());
    out_entry->det = root.getString("det").value_or(std::string());
    out_entry->rec = root.getString("rec").value_or(std::string());
    out_entry->model = root.getString("model").value_or(std::string());
    out_entry->source = path;
    return true;
}

const DefaultPluginCatalogEntry *FindDefaultPluginCatalogEntryByName(const AppRuntime &runtime, const std::string &name) {
    const auto it = std::find_if(runtime.plugin.default_plugin_catalog_entries.begin(),
                                 runtime.plugin.default_plugin_catalog_entries.end(),
                                 [&](const DefaultPluginCatalogEntry &entry) { return entry.name == name; });
    return it == runtime.plugin.default_plugin_catalog_entries.end() ? nullptr : &(*it);
}

std::string DefaultCatalogModelPath(const AppRuntime &runtime,
                                    const std::string &entry_name,
                                    const std::string &fallback) {
    const DefaultPluginCatalogEntry *entry = FindDefaultPluginCatalogEntryByName(runtime, entry_name);
    if (entry == nullptr || entry->onnx.empty()) {
        return fallback;
    }
    return entry->onnx;
}

std::string DefaultCatalogFieldPath(const AppRuntime &runtime,
                                    const std::string &entry_name,
                                    const std::string &field_value,
                                    const std::string &fallback) {
    const DefaultPluginCatalogEntry *entry = FindDefaultPluginCatalogEntryByName(runtime, entry_name);
    if (entry == nullptr || field_value.empty()) {
        return fallback;
    }
    return field_value;
}

}  // namespace

void RefreshAsrProviders(AppRuntime &runtime) {
    runtime.plugin.asr_provider_entries.clear();
    runtime.plugin.asr_provider_entries.push_back(AsrProviderEntry{
        .name = "offline",
        .endpoint = "",
        .api_key = "",
        .model_path = "assets/default_plugins/asr/resources/sense-voice-encoder-int8.onnx",
    });
    runtime.plugin.asr_provider_entries.push_back(AsrProviderEntry{
        .name = "cloud",
        .endpoint = "https://api.openai.com/v1/audio/transcriptions",
        .api_key = "YOUR_API_KEY",
        .model_path = "",
    });
    runtime.plugin.asr_provider_entries.push_back(AsrProviderEntry{
        .name = "hybrid",
        .endpoint = "https://api.openai.com/v1/audio/transcriptions",
        .api_key = "YOUR_API_KEY",
        .model_path = "assets/default_plugins/asr/resources/sense-voice-encoder-int8.onnx",
    });
}

void RefreshOcrModels(AppRuntime &runtime) {
    runtime.plugin.ocr_model_entries.clear();
    runtime.plugin.ocr_model_entries.push_back(OcrModelEntry{
        .name = "ppocr_v5_default",
        .det_path = "assets/PP-OCRv5_server_det_infer.onnx",
        .rec_path = "assets/PP-OCRv5_server_rec_infer.onnx",
        .keys_path = "assets/ppocr_keys.txt",
    });
}

void RefreshDefaultPluginCatalog(AppRuntime &runtime) {
    runtime.plugin.default_plugin_catalog_entries.clear();
    runtime.plugin.default_plugin_catalog_error.clear();

    const std::string base_dir = ResourceLocator::ResolveFirstExisting("assets/default_plugins");
    if (base_dir.empty()) {
        runtime.plugin.default_plugin_catalog_refresh_requested = false;
        runtime.plugin.unified_refresh_requested = true;
        return;
    }

    auto append_scan_error = [&](const std::string &msg) {
        if (msg.empty()) {
            return;
        }
        if (!runtime.plugin.default_plugin_catalog_error.empty()) {
            runtime.plugin.default_plugin_catalog_error += "\n";
        }
        runtime.plugin.default_plugin_catalog_error += msg;
    };

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(base_dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        if (it->path().filename() != "plugin.json") {
            continue;
        }

        DefaultPluginCatalogEntry entry{};
        const std::string path = it->path().generic_string();
        if (!LoadDefaultPluginCatalogEntry(path, &entry)) {
            append_scan_error("default plugin config invalid: " + path);
            continue;
        }
        if (entry.name.empty()) {
            entry.name = it->path().parent_path().filename().string();
        }
        runtime.plugin.default_plugin_catalog_entries.push_back(std::move(entry));
    }

    std::sort(runtime.plugin.default_plugin_catalog_entries.begin(),
              runtime.plugin.default_plugin_catalog_entries.end(),
              [](const DefaultPluginCatalogEntry &lhs, const DefaultPluginCatalogEntry &rhs) {
                  return lhs.name < rhs.name;
              });

    runtime.plugin.default_plugin_catalog_refresh_requested = false;
    runtime.plugin.unified_refresh_requested = true;
}

void RefreshUnifiedPlugins(AppRuntime &runtime) {
    runtime.plugin.unified_entries.clear();
    runtime.plugin.unified_scan_error.clear();

    if (runtime.plugin.default_plugin_catalog_refresh_requested || runtime.plugin.default_plugin_catalog_entries.empty()) {
        RefreshDefaultPluginCatalog(runtime);
    }
    if (runtime.plugin.asr_provider_entries.empty()) {
        RefreshAsrProviders(runtime);
    }
    if (runtime.plugin.ocr_model_entries.empty()) {
        RefreshOcrModels(runtime);
    }
    if (runtime.plugin.config_entries.empty()) {
        RefreshPluginConfigs(runtime);
    }

    for (const auto &entry : runtime.plugin.asr_provider_entries) {
        std::vector<std::string> assets;
        if (!entry.model_path.empty()) {
            assets.push_back(entry.model_path);
        } else if (entry.name == "offline" || entry.name == "hybrid") {
            assets.push_back(DefaultCatalogModelPath(runtime,
                                                     "asr",
                                                     "assets/default_plugins/asr/resources/sense-voice-encoder-int8.onnx"));
        }
        auto plugin = MakeEntry(UnifiedPluginKind::Asr,
                                entry.name,
                                "",
                                entry.name,
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.asr_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.asr_last_error;
        runtime.plugin.unified_entries.push_back(std::move(plugin));
    }

    {
        std::vector<std::string> assets = {
            runtime.plugin.override_scene_model_path.empty()
                ? DefaultCatalogModelPath(runtime, "mobileclip", "assets/mobileclip_image.onnx")
                : runtime.plugin.override_scene_model_path,
            runtime.plugin.override_scene_labels_path.empty()
                ? DefaultCatalogFieldPath(runtime,
                                          "mobileclip",
                                          FindDefaultPluginCatalogEntryByName(runtime, "mobileclip") != nullptr
                                              ? FindDefaultPluginCatalogEntryByName(runtime, "mobileclip")->labels
                                              : std::string(),
                                          "assets/mobileclip_labels.json")
                : runtime.plugin.override_scene_labels_path,
        };
        auto plugin = MakeEntry(UnifiedPluginKind::SceneClassifier,
                                "mobileclip_scene",
                                "",
                                "builtin",
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.perception_state.scene_classifier_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.perception_state.scene_classifier_last_error;
        runtime.plugin.unified_entries.push_back(std::move(plugin));
    }

    {
        std::vector<std::string> assets = {
            runtime.plugin.override_facemesh_model_path.empty()
                ? DefaultCatalogModelPath(runtime,
                                          "facemesh",
                                          "assets/default_plugins/facemesh/resources/facemesh.onnx")
                : runtime.plugin.override_facemesh_model_path,
            runtime.plugin.override_facemesh_labels_path.empty()
                ? DefaultCatalogFieldPath(runtime,
                                          "facemesh",
                                          FindDefaultPluginCatalogEntryByName(runtime, "facemesh") != nullptr
                                              ? FindDefaultPluginCatalogEntryByName(runtime, "facemesh")->labels
                                              : std::string(),
                                          "assets/default_plugins/facemesh/resources/facemesh.labels.json")
                : runtime.plugin.override_facemesh_labels_path,
        };
        auto plugin = MakeEntry(UnifiedPluginKind::Facemesh,
                                "facemesh",
                                "",
                                "builtin",
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.perception_state.camera_facemesh_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.perception_state.camera_facemesh_last_error;
        runtime.plugin.unified_entries.push_back(std::move(plugin));
    }

    for (const auto &entry : runtime.plugin.ocr_model_entries) {
        std::vector<std::string> assets = {
            runtime.plugin.override_ocr_det_path.empty() ? entry.det_path : runtime.plugin.override_ocr_det_path,
            runtime.plugin.override_ocr_rec_path.empty() ? entry.rec_path : runtime.plugin.override_ocr_rec_path,
            runtime.plugin.override_ocr_keys_path.empty() ? entry.keys_path : runtime.plugin.override_ocr_keys_path,
        };
        auto plugin = MakeEntry(UnifiedPluginKind::Ocr,
                                entry.name,
                                "",
                                entry.name,
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.perception_state.ocr_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.perception_state.ocr_last_error;
        runtime.plugin.unified_entries.push_back(std::move(plugin));
    }

    for (const auto &entry : runtime.plugin.config_entries) {
        std::vector<std::string> assets = {entry.config_path};
        auto plugin = MakeEntry(UnifiedPluginKind::BehaviorUser,
                                entry.name,
                                entry.model_version,
                                entry.config_path,
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.plugin.ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.plugin.last_error;
        runtime.plugin.unified_entries.push_back(std::move(plugin));
    }

    std::sort(runtime.plugin.unified_entries.begin(), runtime.plugin.unified_entries.end(),
              [](const UnifiedPluginEntry &a, const UnifiedPluginEntry &b) {
                  if (a.kind != b.kind) {
                      return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                  }
                  return a.name < b.name;
              });

    runtime.plugin.unified_refresh_requested = false;
}

bool DeleteUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error) {
    const auto it = std::find_if(runtime.plugin.unified_entries.begin(), runtime.plugin.unified_entries.end(),
                                 [&](const UnifiedPluginEntry &entry) { return entry.id == id; });
    if (it == runtime.plugin.unified_entries.end()) {
        if (out_error) *out_error = "unified plugin not found: " + id;
        return false;
    }

    if (it->kind != UnifiedPluginKind::BehaviorUser) {
        if (out_error) *out_error = "delete only supported for behavior user plugins";
        return false;
    }

    std::string err;
    const std::filesystem::path config_path = it->source.empty() ? it->name : it->source;
    const bool ok = DeletePluginConfig(runtime, config_path.generic_string(), &err);
    if (!ok) {
        if (out_error) *out_error = err.empty() ? "delete plugin failed" : err;
        const std::string msg = out_error ? *out_error : std::string("delete plugin failed");
        AppendPluginLog(runtime, it->id, PluginLogLevel::Error, msg,
                        ToErrorCode(msg, RuntimeErrorCode::InitFailed));
        return false;
    }

    AppendPluginLog(runtime, it->id, PluginLogLevel::Info, "plugin deleted");
    if (out_error) out_error->clear();
    return true;
}

}  // namespace desktoper2D
