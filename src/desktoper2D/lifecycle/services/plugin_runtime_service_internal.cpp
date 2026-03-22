#include "desktoper2D/lifecycle/services/plugin_runtime_service_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/resource_locator.h"

namespace desktoper2D::plugin_runtime_detail {

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

std::vector<std::string> SplitLinesKeepNonEmpty(const std::string &text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream iss(text);
    while (std::getline(iss, line)) {
        const std::string trimmed = TrimCopy(line);
        if (!trimmed.empty()) {
            out.push_back(trimmed);
        }
    }
    return out;
}

std::vector<std::string> BuildExtraOnnxFromCsvOrLines(const std::string &input) {
    if (input.find('\n') != std::string::npos) {
        return SplitLinesKeepNonEmpty(input);
    }
    return SplitCsv(input);
}

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

bool WriteTextFile(const std::string &path, const std::string &text, std::string *out_error) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        if (out_error) *out_error = "write file failed: " + path;
        return false;
    }
    ofs << text;
    ofs.close();
    if (out_error) out_error->clear();
    return true;
}

JsonValue BuildBehaviorPluginTemplateJson(const std::string &model_id,
                                          const std::string &onnx,
                                          const std::vector<std::string> &extra_onnx) {
    JsonObject root;
    root["model_id"] = JsonValue::makeString(model_id);
    root["model_version"] = JsonValue::makeString("0.1.0");
    root["onnx"] = JsonValue::makeString(onnx);
    JsonArray extra;
    for (const auto &p : extra_onnx) {
        extra.push_back(JsonValue::makeString(p));
    }
    if (!extra.empty()) {
        root["extra_onnx"] = JsonValue::makeArray(std::move(extra));
    }
    JsonArray backend;
    backend.push_back(JsonValue::makeString("cpu"));
    root["backend_priority"] = JsonValue::makeArray(std::move(backend));
    return JsonValue::makeObject(std::move(root));
}

void ReplaceAll(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
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

std::string BuildUserPluginFolderName(const std::string &safe_name) {
    return safe_name.empty() ? "user_plugin" : safe_name;
}

int ToErrorCode(const std::string &detail, RuntimeErrorCode fallback) {
    return static_cast<int>(ClassifyRuntimeErrorCodeFromDetail(detail, fallback));
}

bool TryGetLastWriteTime(const std::string &path,
                        std::filesystem::file_time_type *out_time,
                        bool *out_valid) {
    if (out_valid) {
        *out_valid = false;
    }
    if (out_time) {
        *out_time = std::filesystem::file_time_type{};
    }
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    const auto write_time = std::filesystem::last_write_time(std::filesystem::path(path), ec);
    if (ec) {
        return false;
    }
    if (out_time) {
        *out_time = write_time;
    }
    if (out_valid) {
        *out_valid = true;
    }
    return true;
}

bool BuildPluginConfigCacheEntry(AppRuntime &runtime,
                                 const std::string &config_path,
                                 PluginConfigCacheEntry *out_cache_entry,
                                 bool *out_changed) {
    if (out_changed) {
        *out_changed = false;
    }
    if (!out_cache_entry || config_path.empty()) {
        return false;
    }

    PluginConfigCacheEntry next{};
    next.entry.config_path = config_path;
    TryGetLastWriteTime(config_path, &next.last_write_time, &next.last_write_time_valid);

    const auto cache_it = runtime.plugin.config_entry_cache.find(config_path);
    const bool can_reuse_cache = cache_it != runtime.plugin.config_entry_cache.end() &&
                                 cache_it->second.last_write_time_valid == next.last_write_time_valid &&
                                 (!next.last_write_time_valid || cache_it->second.last_write_time == next.last_write_time);
    if (can_reuse_cache) {
        *out_cache_entry = cache_it->second;
        return cache_it->second.parse_ok;
    }

    std::string model_id;
    std::string model_version;
    std::string onnx;
    std::vector<std::string> extra;
    std::string err;
    const bool parse_ok = LoadPluginConfigFields(config_path, &model_id, &model_version, &onnx, &extra, &err);

    next.parse_ok = parse_ok;
    next.parse_error = parse_ok ? std::string() : err;
    const auto enabled_it = runtime.plugin.enabled_states.find(config_path);
    next.entry.enabled = (enabled_it == runtime.plugin.enabled_states.end()) ? true : enabled_it->second;
    if (parse_ok) {
        next.entry.model_id = model_id;
        next.entry.model_version = model_version;
        next.entry.name = !model_id.empty() ? model_id : std::filesystem::path(config_path).stem().string();
    }

    *out_cache_entry = std::move(next);
    if (out_changed) {
        *out_changed = true;
    }
    return parse_ok;
}

bool PluginConfigEntryEqual(const PluginConfigEntry &lhs, const PluginConfigEntry &rhs) {
    return lhs.name == rhs.name &&
           lhs.config_path == rhs.config_path &&
           lhs.model_id == rhs.model_id &&
           lhs.model_version == rhs.model_version &&
           lhs.enabled == rhs.enabled;
}

PluginRuntimeConfig BuildPluginRuntimeConfig(const AppRuntime &runtime) {
    PluginRuntimeConfig cfg{};
    cfg.show_debug_stats = runtime.show_debug_stats;
    cfg.manual_param_mode = runtime.manual_param_mode;
    cfg.click_through = runtime.window_state.click_through;
    cfg.window_opacity = 1.0f;
    return cfg;
}

PluginHostCallbacks BuildPluginHostCallbacks() {
    PluginHostCallbacks host{};
    host.log = [](void *, const char *msg) { SDL_Log("[PluginHost] %s", msg ? msg : ""); };
    return host;
}

void ApplyPluginInitFailure(AppRuntime &runtime, const std::string &err) {
    runtime.plugin.last_error = err;
    SetPluginError(runtime.plugin.error_info,
                   RuntimeErrorCode::InitFailed,
                   err.empty() ? std::string("plugin init failed") : err);
}

void ApplyPluginHealthy(AppRuntime &runtime) {
    runtime.plugin.last_error.clear();
    ClearRuntimeError(runtime.plugin.error_info);
}

}  // namespace desktoper2D::plugin_runtime_detail
