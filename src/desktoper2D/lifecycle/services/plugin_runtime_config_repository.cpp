#include "desktoper2D/lifecycle/services/plugin_runtime_config_repository.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include "desktoper2D/lifecycle/resource_locator.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service_internal.h"

namespace desktoper2D {

using namespace plugin_runtime_detail;

bool LoadPluginConfigFields(const std::string &config_path,
                            std::string *out_model_id,
                            std::string *out_model_version,
                            std::string *out_onnx,
                            std::vector<std::string> *out_extra_onnx,
                            std::string *out_error) {
    const std::string text = ReadTextFile(config_path);
    if (text.empty()) {
        if (out_error) *out_error = "failed to read plugin config: " + config_path;
        return false;
    }
    JsonParseError parse_err{};
    auto root_opt = ParseJson(text, &parse_err);
    if (!root_opt) {
        if (out_error) *out_error = "plugin config parse failed";
        return false;
    }
    const JsonValue &root = *root_opt;
    if (!root.isObject() || !root.asObject()) {
        if (out_error) *out_error = "plugin config root is not object";
        return false;
    }
    const auto &obj = *root.asObject();
    if (out_model_id) {
        auto it = obj.find("model_id");
        if (it != obj.end()) {
            if (const std::string *v = it->second.asString()) {
                *out_model_id = *v;
            }
        }
    }
    if (out_model_version) {
        auto it = obj.find("model_version");
        if (it != obj.end()) {
            if (const std::string *v = it->second.asString()) {
                *out_model_version = *v;
            }
        }
    }
    if (out_onnx) {
        auto it = obj.find("onnx");
        if (it != obj.end()) {
            if (const std::string *v = it->second.asString()) {
                *out_onnx = *v;
            }
        }
    }
    if (out_extra_onnx) {
        out_extra_onnx->clear();
        auto it = obj.find("extra_onnx");
        if (it != obj.end() && it->second.isArray() && it->second.asArray()) {
            for (const auto &v : *it->second.asArray()) {
                if (const std::string *s = v.asString()) {
                    out_extra_onnx->push_back(*s);
                }
            }
        }
    }
    if (out_error) out_error->clear();
    return true;
}

bool SavePluginConfigFields(const std::string &config_path,
                            const std::string &model_id,
                            const std::string &onnx,
                            const std::vector<std::string> &extra_onnx,
                            std::string *out_error) {
    const JsonValue root = BuildBehaviorPluginTemplateJson(model_id, onnx, extra_onnx);
    const std::string text = StringifyJson(root, 2);
    return WriteTextFile(config_path, text, out_error);
}

void RefreshPluginConfigs(AppRuntime &runtime) {
    runtime.plugin.config_scan_error.clear();

    const std::vector<PluginConfigEntry> previous_entries = runtime.plugin.config_entries;
    std::vector<PluginConfigEntry> refreshed;
    std::unordered_set<std::string> seen_paths;
    std::unordered_map<std::string, PluginConfigCacheEntry> next_cache;
    bool cache_changed = false;

    auto append_scan_error = [&](const std::string &msg) {
        if (msg.empty()) return;
        if (!runtime.plugin.config_scan_error.empty()) {
            runtime.plugin.config_scan_error += "\n";
        }
        runtime.plugin.config_scan_error += msg;
    };

    auto add_config_path = [&](const std::string &config_path) {
        if (config_path.empty()) return;
        if (!seen_paths.insert(config_path).second) return;

        PluginConfigCacheEntry cache_entry{};
        bool entry_changed = false;
        const bool parse_ok = BuildPluginConfigCacheEntry(runtime, config_path, &cache_entry, &entry_changed);
        if (entry_changed) {
            cache_changed = true;
        }
        next_cache[config_path] = cache_entry;

        if (!parse_ok) {
            append_scan_error("plugin config invalid: " + config_path +
                              (cache_entry.parse_error.empty() ? "" : " (" + cache_entry.parse_error + ")"));
            return;
        }

        const auto enabled_it = runtime.plugin.enabled_states.find(config_path);
        cache_entry.entry.enabled = (enabled_it == runtime.plugin.enabled_states.end()) ? true : enabled_it->second;
        next_cache[config_path].entry.enabled = cache_entry.entry.enabled;
        refreshed.push_back(cache_entry.entry);
    };

    const std::string default_config = ResourceLocator::ResolveFirstExisting("assets/plugin_behavior_config.json");
    add_config_path(default_config);

    std::filesystem::path base_dir;
    if (!default_config.empty()) {
        base_dir = std::filesystem::path(default_config).parent_path() / "user_plugins";
    } else {
        const std::string resolved_user_plugins = ResourceLocator::ResolveFirstExisting("assets/user_plugins");
        if (!resolved_user_plugins.empty()) {
            base_dir = resolved_user_plugins;
        } else {
            base_dir = std::filesystem::path("assets") / "user_plugins";
        }
    }

    std::error_code ec;
    if (!base_dir.empty() && std::filesystem::exists(base_dir, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(base_dir, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            if (it->path().filename() == "plugin.json") {
                add_config_path(it->path().generic_string());
            }
        }
    }

    if (runtime.plugin.config_entry_cache.size() != next_cache.size()) {
        cache_changed = true;
    } else {
        for (const auto &kv : runtime.plugin.config_entry_cache) {
            if (!next_cache.count(kv.first)) {
                cache_changed = true;
                break;
            }
        }
    }
    runtime.plugin.config_entry_cache = std::move(next_cache);
    runtime.plugin.config_entries = std::move(refreshed);

    std::unordered_set<std::string> known_paths;
    known_paths.reserve(runtime.plugin.config_entries.size());
    for (const auto &entry : runtime.plugin.config_entries) {
        known_paths.insert(entry.config_path);
    }
    for (auto it = runtime.plugin.enabled_states.begin(); it != runtime.plugin.enabled_states.end();) {
        if (!known_paths.count(it->first)) {
            it = runtime.plugin.enabled_states.erase(it);
            cache_changed = true;
        } else {
            ++it;
        }
    }

    bool entries_changed = previous_entries.size() != runtime.plugin.config_entries.size();
    if (!entries_changed) {
        for (std::size_t i = 0; i < previous_entries.size(); ++i) {
            if (!PluginConfigEntryEqual(previous_entries[i], runtime.plugin.config_entries[i])) {
                entries_changed = true;
                break;
            }
        }
    }
    if (entries_changed || cache_changed) {
        runtime.plugin.unified_refresh_requested = true;
    }

    runtime.plugin.config_refresh_requested = false;
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
        std::error_code rm_ec;
        std::filesystem::remove_all(path, rm_ec);
        if (rm_ec) {
            if (out_error) *out_error = "delete plugin folder failed: " + rm_ec.message();
            return false;
        }
    } else {
        std::filesystem::remove(path, ec);
        if (ec) {
            if (out_error) *out_error = "delete plugin config failed: " + ec.message();
            return false;
        }
        const std::filesystem::path parent_dir = path.parent_path();
        if (parent_dir.parent_path().filename() == "user_plugins") {
            std::filesystem::remove_all(parent_dir, ec);
        }
    }

    runtime.plugin.config_entries.erase(
        std::remove_if(runtime.plugin.config_entries.begin(), runtime.plugin.config_entries.end(),
                       [&](const PluginConfigEntry &entry) { return entry.config_path == config_path; }),
        runtime.plugin.config_entries.end());
    runtime.plugin.config_entry_cache.erase(config_path);
    if (runtime.plugin.selected_entry_index >= static_cast<int>(runtime.plugin.config_entries.size())) {
        runtime.plugin.selected_entry_index = static_cast<int>(runtime.plugin.config_entries.size()) - 1;
    }
    if (runtime.plugin.selected_entry_index < 0) {
        runtime.plugin.selected_entry_index = -1;
    }
    RemovePluginEnabledState(runtime, config_path);
    runtime.plugin.unified_refresh_requested = true;
    return true;
}

void SetPluginEnabledState(AppRuntime &runtime, const std::string &config_path, bool enabled) {
    runtime.plugin.enabled_states[config_path] = enabled;
    for (auto &entry : runtime.plugin.config_entries) {
        if (entry.config_path == config_path) {
            entry.enabled = enabled;
            break;
        }
    }
}

void RemovePluginEnabledState(AppRuntime &runtime, const std::string &config_path) {
    runtime.plugin.enabled_states.erase(config_path);
}

bool CreateUserPlugin(AppRuntime &runtime, const UserPluginCreateRequest &request, std::string *out_error) {
    const std::string safe_name = SanitizeName(request.name);
    if (safe_name.empty()) {
        if (out_error) *out_error = "invalid plugin name";
        return false;
    }

    const std::string tpl_path = request.template_path;
    std::string tpl;
    std::filesystem::path assets_dir;
    if (!tpl_path.empty()) {
        const std::string resolved_tpl_path = ResourceLocator::ResolveFirstExisting(tpl_path);
        if (resolved_tpl_path.empty()) {
            if (out_error) *out_error = "template not found: " + tpl_path;
            return false;
        }
        assets_dir = std::filesystem::path(resolved_tpl_path).parent_path();
        tpl = ReadTextFile(resolved_tpl_path);
        if (tpl.empty()) {
            if (out_error) *out_error = "template not found: " + resolved_tpl_path;
            return false;
        }
    } else {
        const std::string resolved_assets_dir = ResourceLocator::ResolveFirstExisting("assets");
        if (!resolved_assets_dir.empty()) {
            assets_dir = std::filesystem::path(resolved_assets_dir);
        } else {
            assets_dir = std::filesystem::path("assets");
        }
    }

    const std::filesystem::path base_dir = assets_dir / "user_plugins";
    std::error_code ec;
    std::filesystem::create_directories(base_dir, ec);
    if (ec) {
        if (out_error) *out_error = "create directory failed: " + ec.message();
        return false;
    }

    const std::string folder_name = BuildUserPluginFolderName(safe_name);
    std::filesystem::path plugin_dir = base_dir / folder_name;
    int suffix = 1;
    while (std::filesystem::exists(plugin_dir, ec)) {
        plugin_dir = base_dir / (folder_name + "_" + std::to_string(suffix++));
    }

    std::filesystem::create_directories(plugin_dir / "resources", ec);
    if (ec) {
        if (out_error) *out_error = "create plugin folder failed: " + ec.message();
        return false;
    }

    const std::filesystem::path config_path = plugin_dir / "plugin.json";

    if (!tpl.empty()) {
        ReplaceAll(tpl, "\"model_id\": \"default_behavior\"",
                   "\"model_id\": \"" + safe_name + "\"");
        ReplaceAll(tpl, "\"model_version\": \"0.1.0\"",
                   "\"model_version\": \"0.1.0\"");
        ReplaceAll(tpl, "assets/behavior/default_behavior.onnx",
                   (plugin_dir / "resources" / (safe_name + ".onnx")).generic_string());

        std::ofstream ofs(config_path, std::ios::binary);
        if (!ofs) {
            if (out_error) *out_error = "write plugin config failed: " + config_path.string();
            return false;
        }
        ofs << tpl;
        ofs.close();
    } else {
        const std::string onnx_path = (plugin_dir / "resources" / (safe_name + ".onnx")).generic_string();
        std::vector<std::string> extra_onnx;
        if (!SavePluginConfigFields(config_path.generic_string(), safe_name, onnx_path, extra_onnx, out_error)) {
            return false;
        }
    }

    const std::string id = "behavior:" + config_path.generic_string();
    AppendPluginLog(runtime, id, PluginLogLevel::Info,
                    "created user plugin config: " + config_path.generic_string());

    runtime.plugin.config_entries.push_back(PluginConfigEntry{
        .name = safe_name,
        .config_path = config_path.generic_string(),
        .model_id = safe_name,
        .model_version = "0.1.0",
        .enabled = true,
    });
    runtime.plugin.enabled_states[config_path.generic_string()] = true;
    runtime.plugin.config_entry_cache.erase(config_path.generic_string());
    runtime.plugin.unified_refresh_requested = true;

    if (out_error) out_error->clear();
    return true;
}

}  // namespace desktoper2D
