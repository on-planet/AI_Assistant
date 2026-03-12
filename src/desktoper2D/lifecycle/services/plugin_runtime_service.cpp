#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tuple>

#include "desktoper2D/lifecycle/asr/cloud_asr_provider.h"
#include "desktoper2D/lifecycle/asr/hybrid_asr_provider.h"
#include "desktoper2D/lifecycle/asr/offline_asr_provider.h"
#include "desktoper2D/lifecycle/inference_adapter.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/plugins/onnx_behavior_plugin.h"
#include "desktoper2D/lifecycle/resource_locator.h"

namespace desktoper2D {

namespace {

std::string ToString(UnifiedPluginKind kind) {
    switch (kind) {
        case UnifiedPluginKind::Asr:
            return "asr";
        case UnifiedPluginKind::Facemesh:
            return "facemesh";
        case UnifiedPluginKind::SceneClassifier:
            return "scene";
        case UnifiedPluginKind::Ocr:
            return "ocr";
        case UnifiedPluginKind::BehaviorUser:
            return "behavior";
        default:
            return "unknown";
    }
}

std::string ToString(UnifiedPluginStatus status) {
    switch (status) {
        case UnifiedPluginStatus::NotLoaded:
            return "not_loaded";
        case UnifiedPluginStatus::Loading:
            return "loading";
        case UnifiedPluginStatus::Ready:
            return "ready";
        case UnifiedPluginStatus::Error:
            return "error";
        case UnifiedPluginStatus::Disabled:
            return "disabled";
        default:
            return "unknown";
    }
}

std::vector<std::tuple<std::string, std::string, std::string>> BuildOcrCandidateTriplesForPaths(
    const std::string &det_path,
    const std::string &rec_path,
    const std::string &keys_path) {
    std::vector<std::tuple<std::string, std::string, std::string>> out;
    if (det_path.empty() || rec_path.empty() || keys_path.empty()) {
        return out;
    }

    std::vector<std::tuple<std::string, std::string, std::string>> relative_triples;
    relative_triples.emplace_back(det_path, rec_path, keys_path);
    if (keys_path == "assets/ppocr_keys.txt") {
        relative_triples.emplace_back(det_path, rec_path, "assets/ocr/ppocr_keys.txt");
    } else if (keys_path == "assets/ocr/ppocr_keys.txt") {
        relative_triples.emplace_back(det_path, rec_path, "assets/ppocr_keys.txt");
    }

    for (const auto &triple : relative_triples) {
        auto candidates = ResourceLocator::BuildCandidateTriples(
            std::get<0>(triple),
            std::get<1>(triple),
            std::get<2>(triple));
        out.insert(out.end(), candidates.begin(), candidates.end());
    }
    return out;
}

UnifiedPluginEntry MakeEntry(UnifiedPluginKind kind,
                             const std::string &name,
                             const std::string &version,
                             const std::string &source,
                             const std::vector<std::string> &assets,
                             const std::string &backend) {
    UnifiedPluginEntry entry{};
    entry.kind = kind;
    entry.name = name;
    entry.version = version;
    entry.source = source;
    entry.assets = assets;
    entry.backend = backend;
    entry.id = ToString(kind) + ":" + (source.empty() ? name : source);
    entry.status = UnifiedPluginStatus::NotLoaded;
    return entry;
}

std::string ReadTextFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

void ReplaceAll(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string TrimCopy(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string> SplitCsv(const std::string &csv) {
    std::vector<std::string> out;
    std::string token;
    std::istringstream iss(csv);
    while (std::getline(iss, token, ',')) {
        const std::string trimmed = TrimCopy(token);
        if (!trimmed.empty()) {
            out.push_back(trimmed);
        }
    }
    return out;
}

bool ReplaceJsonStringValue(std::string &text, const std::string &key, const std::string &value) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find('"', pos);
    if (pos == std::string::npos) {
        return false;
    }
    const std::size_t start = pos + 1;
    std::size_t end = text.find('"', start);
    if (end == std::string::npos) {
        return false;
    }
    text.replace(start, end - start, value);
    return true;
}

bool ReplaceJsonStringArray(std::string &text, const std::string &key, const std::vector<std::string> &values) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = text.find('[', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    const std::size_t start = pos + 1;
    std::size_t end = text.find(']', start);
    if (end == std::string::npos) {
        return false;
    }
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "\"" << values[i] << "\"";
    }
    text.replace(start, end - start, oss.str());
    return true;
}

std::string SanitizeName(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "user_plugin";
    }
    return out;
}

int ToErrorCode(const std::string &detail, RuntimeErrorCode fallback) {
    return static_cast<int>(ClassifyRuntimeErrorCodeFromDetail(detail, fallback));
}

PluginRuntimeConfig BuildPluginRuntimeConfig(const AppRuntime &runtime) {
    PluginRuntimeConfig cfg{};
    cfg.show_debug_stats = runtime.show_debug_stats;
    cfg.manual_param_mode = runtime.manual_param_mode;
    cfg.click_through = runtime.click_through;
    cfg.window_opacity = 1.0f;
    return cfg;
}

PluginHostCallbacks BuildPluginHostCallbacks() {
    PluginHostCallbacks host{};
    host.log = [](void *, const char *msg) { SDL_Log("[PluginHost] %s", msg ? msg : ""); };
    return host;
}

void ApplyPluginInitFailure(AppRuntime &runtime, const std::string &err) {
    runtime.plugin_last_error = err;
    SetPluginError(runtime.plugin_error_info,
                   RuntimeErrorCode::InitFailed,
                   err.empty() ? std::string("plugin init failed") : err);
}

void ApplyPluginHealthy(AppRuntime &runtime) {
    runtime.plugin_last_error.clear();
    ClearRuntimeError(runtime.plugin_error_info);
}

}  // namespace

void RefreshPluginConfigs(AppRuntime &runtime) {
    runtime.plugin_config_entries.clear();
    runtime.plugin_config_scan_error.clear();

    std::vector<std::filesystem::path> roots;
    roots.emplace_back(std::filesystem::path("assets"));
    for (const auto &root : ResourceLocator::BuildSearchRoots(6)) {
        if (root.empty()) {
            continue;
        }
        roots.emplace_back(root / "assets");
    }

    std::error_code ec;
    for (const auto &root : roots) {
        if (root.empty() || !std::filesystem::exists(root, ec)) {
            continue;
        }
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (ec) {
                runtime.plugin_config_scan_error = "scan error: " + ec.message();
                break;
            }
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const auto &path = entry.path();
            if (path.extension() != ".json") {
                continue;
            }
            std::string err;
            PluginArtifactSpec spec{};
            auto plugin = CreateOnnxBehaviorPluginFromConfig(path.generic_string(), &err, &spec);
            if (!plugin) {
                continue;
            }
            const std::string name = spec.model_id.empty() ? path.stem().string() : spec.model_id;
            runtime.plugin_config_entries.push_back(PluginConfigEntry{
                .name = name,
                .config_path = path.generic_string(),
                .model_id = spec.model_id,
                .model_version = spec.model_version,
            });
        }
    }

    std::sort(runtime.plugin_config_entries.begin(), runtime.plugin_config_entries.end(),
              [](const PluginConfigEntry &a, const PluginConfigEntry &b) {
                  return a.name < b.name;
              });
    runtime.plugin_config_entries.erase(
        std::unique(runtime.plugin_config_entries.begin(), runtime.plugin_config_entries.end(),
                    [](const PluginConfigEntry &a, const PluginConfigEntry &b) {
                        return a.name == b.name && a.config_path == b.config_path;
                    }),
        runtime.plugin_config_entries.end());

    runtime.plugin_config_refresh_requested = false;
}

bool SwitchPluginByName(AppRuntime &runtime, const std::string &name, std::string *out_error) {
    if (!runtime.inference_adapter) {
        if (out_error) *out_error = "plugin adapter not initialized";
        return false;
    }
    const auto it = std::find_if(runtime.plugin_config_entries.begin(), runtime.plugin_config_entries.end(),
                                 [&](const PluginConfigEntry &entry) { return entry.name == name; });
    if (it == runtime.plugin_config_entries.end()) {
        if (out_error) *out_error = "plugin not found: " + name;
        return false;
    }
    std::string err;
    const bool ok = runtime.inference_adapter->SwitchPluginByConfigPath(it->config_path, &err);
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

void RefreshAsrProviders(AppRuntime &runtime) {
    runtime.asr_provider_entries.clear();
    runtime.asr_provider_entries.push_back(AsrProviderEntry{
        .name = "offline",
        .endpoint = "",
        .api_key = "",
        .model_path = "assets/sense-voice-encoder-int8.onnx",
    });
    runtime.asr_provider_entries.push_back(AsrProviderEntry{
        .name = "cloud",
        .endpoint = "https://api.openai.com/v1/audio/transcriptions",
        .api_key = "YOUR_API_KEY",
        .model_path = "",
    });
    runtime.asr_provider_entries.push_back(AsrProviderEntry{
        .name = "hybrid",
        .endpoint = "https://api.openai.com/v1/audio/transcriptions",
        .api_key = "YOUR_API_KEY",
        .model_path = "assets/sense-voice-encoder-int8.onnx",
    });
}

bool SwitchAsrProviderByName(AppRuntime &runtime, const std::string &name, std::string *out_error) {
    auto it = std::find_if(runtime.asr_provider_entries.begin(), runtime.asr_provider_entries.end(),
                           [&](const AsrProviderEntry &entry) { return entry.name == name; });
    if (it == runtime.asr_provider_entries.end()) {
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

    if (runtime.asr_provider) {
        runtime.asr_provider->Shutdown();
    }
    runtime.asr_provider = std::move(provider);
    runtime.asr_ready = true;
    runtime.asr_last_error.clear();
    ClearRuntimeError(runtime.asr_error_info);
    AppendPluginLog(runtime, "asr:" + it->name, PluginLogLevel::Info, "asr switch ok");
    if (out_error) out_error->clear();
    return true;
}

void RefreshOcrModels(AppRuntime &runtime) {
    runtime.ocr_model_entries.clear();
    runtime.ocr_model_entries.push_back(OcrModelEntry{
        .name = "ppocr_v5_default",
        .det_path = "assets/PP-OCRv5_server_det_infer.onnx",
        .rec_path = "assets/PP-OCRv5_server_rec_infer.onnx",
        .keys_path = "assets/ppocr_keys.txt",
    });
}

void RefreshUnifiedPlugins(AppRuntime &runtime) {
    runtime.unified_plugin_entries.clear();
    runtime.unified_plugin_scan_error.clear();

    if (runtime.asr_provider_entries.empty()) {
        RefreshAsrProviders(runtime);
    }
    if (runtime.ocr_model_entries.empty()) {
        RefreshOcrModels(runtime);
    }
    if (runtime.plugin_config_entries.empty()) {
        RefreshPluginConfigs(runtime);
    }

    for (const auto &entry : runtime.asr_provider_entries) {
        std::vector<std::string> assets;
        if (!entry.model_path.empty()) {
            assets.push_back(entry.model_path);
        }
        auto plugin = MakeEntry(UnifiedPluginKind::Asr,
                                entry.name,
                                "",
                                entry.name,
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.asr_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.asr_last_error;
        runtime.unified_plugin_entries.push_back(std::move(plugin));
    }

    {
        std::vector<std::string> assets = {
            "assets/mobileclip_image.onnx",
            "assets/mobileclip_labels.json",
        };
        auto plugin = MakeEntry(UnifiedPluginKind::SceneClassifier,
                                "mobileclip_scene",
                                "",
                                "builtin",
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.perception_state.scene_classifier_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.perception_state.scene_classifier_last_error;
        runtime.unified_plugin_entries.push_back(std::move(plugin));
    }

    {
        std::vector<std::string> assets = {
            "assets/facemesh.onnx",
            "assets/facemesh.labels.json",
        };
        auto plugin = MakeEntry(UnifiedPluginKind::Facemesh,
                                "facemesh",
                                "",
                                "builtin",
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.perception_state.camera_facemesh_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.perception_state.camera_facemesh_last_error;
        runtime.unified_plugin_entries.push_back(std::move(plugin));
    }

    for (const auto &entry : runtime.ocr_model_entries) {
        std::vector<std::string> assets = {entry.det_path, entry.rec_path, entry.keys_path};
        auto plugin = MakeEntry(UnifiedPluginKind::Ocr,
                                entry.name,
                                "",
                                entry.name,
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.perception_state.ocr_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.perception_state.ocr_last_error;
        runtime.unified_plugin_entries.push_back(std::move(plugin));
    }

    for (const auto &entry : runtime.plugin_config_entries) {
        std::vector<std::string> assets = {entry.config_path};
        auto plugin = MakeEntry(UnifiedPluginKind::BehaviorUser,
                                entry.name,
                                entry.model_version,
                                entry.config_path,
                                assets,
                                "onnxruntime.cpu");
        plugin.status = runtime.plugin_ready ? UnifiedPluginStatus::Ready : UnifiedPluginStatus::NotLoaded;
        plugin.last_error = runtime.plugin_last_error;
        runtime.unified_plugin_entries.push_back(std::move(plugin));
    }

    std::sort(runtime.unified_plugin_entries.begin(), runtime.unified_plugin_entries.end(),
              [](const UnifiedPluginEntry &a, const UnifiedPluginEntry &b) {
                  if (a.kind != b.kind) {
                      return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                  }
                  return a.name < b.name;
              });

    runtime.unified_plugin_refresh_requested = false;
}

bool SwitchUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error) {
    const auto it = std::find_if(runtime.unified_plugin_entries.begin(), runtime.unified_plugin_entries.end(),
                                 [&](const UnifiedPluginEntry &entry) { return entry.id == id; });
    if (it == runtime.unified_plugin_entries.end()) {
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
        ok = false;
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

bool DeleteUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error) {
    const auto it = std::find_if(runtime.unified_plugin_entries.begin(), runtime.unified_plugin_entries.end(),
                                 [&](const UnifiedPluginEntry &entry) { return entry.id == id; });
    if (it == runtime.unified_plugin_entries.end()) {
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

bool ReplaceUnifiedPluginAssets(AppRuntime &runtime,
                                const std::string &id,
                                const PluginAssetOverride &override,
                                std::string *out_error) {
    const auto it = std::find_if(runtime.unified_plugin_entries.begin(), runtime.unified_plugin_entries.end(),
                                 [&](const UnifiedPluginEntry &entry) { return entry.id == id; });
    if (it == runtime.unified_plugin_entries.end()) {
        if (out_error) *out_error = "unified plugin not found: " + id;
        return false;
    }

    std::string err;
    bool ok = false;
    if (it->kind == UnifiedPluginKind::SceneClassifier) {
        runtime.override_scene_model_path = override.onnx;
        runtime.override_scene_labels_path = override.labels;
        ok = ApplyOverrideModels(runtime, &err);
    } else if (it->kind == UnifiedPluginKind::Facemesh) {
        runtime.override_facemesh_model_path = override.onnx;
        runtime.override_facemesh_labels_path = override.labels;
        ok = ApplyOverrideModels(runtime, &err);
    } else if (it->kind == UnifiedPluginKind::Ocr) {
        runtime.override_ocr_det_path = override.onnx;
        runtime.override_ocr_rec_path = override.labels;
        runtime.override_ocr_keys_path = override.keys;
        ok = ApplyOverrideModels(runtime, &err);
    } else if (it->kind == UnifiedPluginKind::Asr) {
        runtime.override_asr_model_path = override.onnx;
        ok = ApplyOverrideModels(runtime, &err);
    } else if (it->kind == UnifiedPluginKind::BehaviorUser) {
        const std::filesystem::path config_path = it->source.empty() ? it->name : it->source;
        std::string tpl = ReadTextFile(config_path.generic_string());
        if (tpl.empty()) {
            err = "failed to read plugin config: " + config_path.generic_string();
            ok = false;
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
                ok = false;
            } else {
                ofs << tpl;
                ofs.close();
                ok = SwitchUnifiedPluginById(runtime, id, &err);
            }
        }
    } else {
        err = "override not supported for kind: " + ToString(it->kind);
        ok = false;
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

void AppendPluginLog(AppRuntime &runtime,
                     const std::string &id,
                     PluginLogLevel level,
                     const std::string &message,
                     int error_code) {
    auto &queue = runtime.plugin_logs[id];
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

bool DeletePluginConfig(AppRuntime &runtime, const std::string &config_path, std::string *out_error) {
    if (config_path.empty()) {
        if (out_error) *out_error = "plugin config path is empty";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::path(config_path);
    if (!std::filesystem::exists(path, ec)) {
        if (out_error) *out_error = "plugin config not found: " + config_path;
        return false;
    }
    if (std::filesystem::is_directory(path, ec)) {
        if (out_error) *out_error = "plugin config is a directory: " + config_path;
        return false;
    }
    std::filesystem::remove(path, ec);
    if (ec) {
        if (out_error) *out_error = "delete plugin config failed: " + ec.message();
        return false;
    }

    runtime.plugin_config_entries.erase(
        std::remove_if(runtime.plugin_config_entries.begin(), runtime.plugin_config_entries.end(),
                       [&](const PluginConfigEntry &entry) { return entry.config_path == config_path; }),
        runtime.plugin_config_entries.end());
    if (runtime.plugin_selected_entry_index >= static_cast<int>(runtime.plugin_config_entries.size())) {
        runtime.plugin_selected_entry_index = static_cast<int>(runtime.plugin_config_entries.size()) - 1;
    }
    if (runtime.plugin_selected_entry_index < 0) {
        runtime.plugin_selected_entry_index = -1;
    }
    runtime.unified_plugin_refresh_requested = true;
    return true;
}

bool CreateUserPlugin(AppRuntime &runtime, const UserPluginCreateRequest &request, std::string *out_error) {
    const std::string safe_name = SanitizeName(request.name);
    if (safe_name.empty()) {
        if (out_error) *out_error = "invalid plugin name";
        return false;
    }

    const std::filesystem::path dir = std::filesystem::path("assets") / "user_plugins";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (out_error) *out_error = "create directory failed: " + ec.message();
        return false;
    }

    std::filesystem::path config_path = dir / (safe_name + ".json");
    int suffix = 1;
    while (std::filesystem::exists(config_path, ec)) {
        config_path = dir / (safe_name + "_" + std::to_string(suffix++) + ".json");
    }

    const std::string tpl_path = request.template_path.empty()
                                    ? std::string("assets/plugin_behavior_config.json")
                                    : request.template_path;
    std::string tpl = ReadTextFile(tpl_path);
    if (tpl.empty()) {
        if (out_error) *out_error = "template not found: " + tpl_path;
        return false;
    }

    ReplaceAll(tpl, "\"model_id\": \"default_behavior\"",
               "\"model_id\": \"" + safe_name + "\"");
    ReplaceAll(tpl, "\"model_version\": \"0.1.0\"",
               "\"model_version\": \"0.1.0\"");
    ReplaceAll(tpl, "assets/behavior/default_behavior.onnx",
               "assets/behavior/" + safe_name + ".onnx");

    std::ofstream ofs(config_path, std::ios::binary);
    if (!ofs) {
        if (out_error) *out_error = "write plugin config failed: " + config_path.string();
        return false;
    }
    ofs << tpl;
    ofs.close();

    const std::string id = "behavior:" + config_path.generic_string();
    AppendPluginLog(runtime, id, PluginLogLevel::Info,
                    "created user plugin config: " + config_path.generic_string());

    RefreshPluginConfigs(runtime);
    RefreshUnifiedPlugins(runtime);

    if (out_error) out_error->clear();
    return true;
}

bool ApplyOverrideModels(AppRuntime &runtime, std::string *out_error) {
    const std::string asr_model = runtime.override_asr_model_path;
    const std::string scene_model = runtime.override_scene_model_path;
    const std::string scene_labels = runtime.override_scene_labels_path;
    const std::string face_model = runtime.override_facemesh_model_path;
    const std::string face_labels = runtime.override_facemesh_labels_path;
    const std::string ocr_det = runtime.override_ocr_det_path;
    const std::string ocr_rec = runtime.override_ocr_rec_path;
    const std::string ocr_keys = runtime.override_ocr_keys_path;

    if (!asr_model.empty()) {
        for (auto &entry : runtime.asr_provider_entries) {
            if (entry.name == "offline" || entry.name == "hybrid") {
                entry.model_path = asr_model;
            }
        }
        std::string err;
        if (!runtime.asr_current_provider_name.empty()) {
            if (!SwitchAsrProviderByName(runtime, runtime.asr_current_provider_name, &err)) {
                if (out_error) *out_error = err;
                AppendPluginLog(runtime, "asr:" + runtime.asr_current_provider_name, PluginLogLevel::Error, err,
                                ToErrorCode(err, RuntimeErrorCode::InitFailed));
                return false;
            }
        }
    }

    std::string perception_err;
    const auto scene_model_candidates = ResourceLocator::BuildCandidatePairs(
        scene_model.empty() ? "assets/mobileclip_image.onnx" : scene_model,
        scene_labels.empty() ? "assets/mobileclip_labels.json" : scene_labels);
    std::vector<std::tuple<std::string, std::string, std::string>> ocr_candidates;
    if (!ocr_det.empty() && !ocr_rec.empty() && !ocr_keys.empty()) {
        ocr_candidates = BuildOcrCandidateTriplesForPaths(ocr_det, ocr_rec, ocr_keys);
    } else if (!runtime.ocr_model_entries.empty()) {
        const auto &entry = runtime.ocr_model_entries.front();
        ocr_candidates = BuildOcrCandidateTriplesForPaths(entry.det_path, entry.rec_path, entry.keys_path);
    }
    const auto facemesh_candidates = ResourceLocator::BuildCandidatePairs(
        face_model.empty() ? "assets/facemesh.onnx" : face_model,
        face_labels.empty() ? "assets/facemesh.labels.json" : face_labels);

    runtime.perception_pipeline.Shutdown(runtime.perception_state);
    const bool ok = runtime.perception_pipeline.Init(runtime.perception_state,
                                                    scene_model_candidates,
                                                    ocr_candidates,
                                                    facemesh_candidates,
                                                    &perception_err);
    if (!ok) {
        const std::string msg = perception_err.empty() ? "override apply failed" : perception_err;
        if (out_error) *out_error = msg;
        const int code = ToErrorCode(msg, RuntimeErrorCode::InitFailed);
        AppendPluginLog(runtime, "scene:builtin", PluginLogLevel::Error, msg, code);
        AppendPluginLog(runtime, "facemesh:builtin", PluginLogLevel::Error, msg, code);
        AppendPluginLog(runtime, "ocr:override", PluginLogLevel::Error, msg, code);
        return false;
    }

    AppendPluginLog(runtime, "scene:builtin", PluginLogLevel::Info, "override applied");
    AppendPluginLog(runtime, "facemesh:builtin", PluginLogLevel::Info, "override applied");
    AppendPluginLog(runtime, "ocr:override", PluginLogLevel::Info, "override applied");

    if (out_error) out_error->clear();
    return true;
}

bool SwitchOcrModelByName(AppRuntime &runtime, const std::string &name, std::string *out_error) {
    auto it = std::find_if(runtime.ocr_model_entries.begin(), runtime.ocr_model_entries.end(),
                           [&](const OcrModelEntry &entry) { return entry.name == name; });
    if (it == runtime.ocr_model_entries.end()) {
        if (out_error) *out_error = "ocr model not found: " + name;
        return false;
    }

    std::string perception_err;
    const auto scene_model_candidates = ResourceLocator::BuildCandidatePairs(
        "assets/mobileclip_image.onnx",
        "assets/mobileclip_labels.json");
    const auto ocr_candidates = BuildOcrCandidateTriplesForPaths(it->det_path, it->rec_path, it->keys_path);
    const auto facemesh_candidates = ResourceLocator::BuildCandidatePairs(
        "assets/facemesh.onnx",
        "assets/facemesh.labels.json");

    runtime.perception_pipeline.Shutdown(runtime.perception_state);
    const bool ok = runtime.perception_pipeline.Init(runtime.perception_state,
                                                    scene_model_candidates,
                                                    ocr_candidates,
                                                    facemesh_candidates,
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

void UpdatePluginLifecycle(AppRuntime &runtime) {
    if (runtime.plugin_config_refresh_requested || runtime.plugin_config_entries.empty()) {
        RefreshPluginConfigs(runtime);
    }
    if (runtime.asr_provider_entries.empty()) {
        RefreshAsrProviders(runtime);
    }
    if (runtime.ocr_model_entries.empty()) {
        RefreshOcrModels(runtime);
    }
    if (runtime.unified_plugin_refresh_requested || runtime.unified_plugin_entries.empty()) {
        RefreshUnifiedPlugins(runtime);
    }

    const bool want_enabled = runtime.feature_plugin_enabled;
    const bool is_ready = runtime.plugin_ready;

    if (want_enabled) {
        if (!runtime.inference_adapter) {
            const auto plugin_config_candidates = ResourceLocator::BuildCandidatePaths(
                "assets/plugin_behavior_config.json");
            runtime.inference_adapter = CreateDefaultInferenceAdapter(plugin_config_candidates);
        }
        if (!is_ready && runtime.inference_adapter) {
            std::string err;
            const PluginRuntimeConfig cfg = BuildPluginRuntimeConfig(runtime);
            const PluginHostCallbacks host = BuildPluginHostCallbacks();
            const PluginWorkerConfig worker_cfg = runtime.inference_adapter->GetWorkerConfig();
            runtime.plugin_ready = runtime.inference_adapter->Init(cfg, host, worker_cfg, &err);
            if (!runtime.plugin_ready) {
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

    if (runtime.inference_adapter) {
        runtime.inference_adapter->Shutdown();
        runtime.inference_adapter.reset();
    }
    runtime.plugin_ready = false;
    runtime.plugin_last_error = "plugin disabled";
    ClearRuntimeError(runtime.plugin_error_info);
}

}  // namespace desktoper2D
