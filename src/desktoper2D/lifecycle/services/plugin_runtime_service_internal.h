#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

#include "desktoper2D/core/json.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_service.h"

namespace desktoper2D::plugin_runtime_detail {

std::string TrimCopy(const std::string &s);
std::vector<std::string> SplitCsv(const std::string &csv);
std::vector<std::string> SplitLinesKeepNonEmpty(const std::string &text);
std::vector<std::string> BuildExtraOnnxFromCsvOrLines(const std::string &input);

std::string ToString(UnifiedPluginKind kind);
std::string ToString(UnifiedPluginStatus status);

std::vector<std::tuple<std::string, std::string, std::string>> BuildOcrCandidateTriplesForPaths(
    const std::string &det_path,
    const std::string &rec_path,
    const std::string &keys_path);

UnifiedPluginEntry MakeEntry(UnifiedPluginKind kind,
                             const std::string &name,
                             const std::string &version,
                             const std::string &source,
                             const std::vector<std::string> &assets,
                             const std::string &backend);

std::string ReadTextFile(const std::string &path);
bool WriteTextFile(const std::string &path, const std::string &text, std::string *out_error);
JsonValue BuildBehaviorPluginTemplateJson(const std::string &model_id,
                                          const std::string &onnx,
                                          const std::vector<std::string> &extra_onnx);
void ReplaceAll(std::string &s, const std::string &from, const std::string &to);
bool ReplaceJsonStringValue(std::string &text, const std::string &key, const std::string &value);
bool ReplaceJsonStringArray(std::string &text, const std::string &key, const std::vector<std::string> &values);
std::string SanitizeName(const std::string &name);
std::string BuildUserPluginFolderName(const std::string &safe_name);

int ToErrorCode(const std::string &detail, RuntimeErrorCode fallback);

bool TryGetLastWriteTime(const std::string &path,
                        std::filesystem::file_time_type *out_time,
                        bool *out_valid);
bool BuildPluginConfigCacheEntry(AppRuntime &runtime,
                                 const std::string &config_path,
                                 PluginConfigCacheEntry *out_cache_entry,
                                 bool *out_changed);
bool PluginConfigEntryEqual(const PluginConfigEntry &lhs, const PluginConfigEntry &rhs);

PluginRuntimeConfig BuildPluginRuntimeConfig(const AppRuntime &runtime);
PluginHostCallbacks BuildPluginHostCallbacks();
void ApplyPluginInitFailure(AppRuntime &runtime, const std::string &err);
void ApplyPluginHealthy(AppRuntime &runtime);

}  // namespace desktoper2D::plugin_runtime_detail
