#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

// 插件生命周期服务：将启停/初始化逻辑从 UI 行为剥离。
void UpdatePluginLifecycle(AppRuntime &runtime);

void RefreshPluginConfigs(AppRuntime &runtime);
bool SwitchPluginByName(AppRuntime &runtime, const std::string &name, std::string *out_error = nullptr);
bool ReloadPluginByConfigPath(AppRuntime &runtime, const std::string &config_path, std::string *out_error = nullptr);
bool DeletePluginConfig(AppRuntime &runtime, const std::string &config_path, std::string *out_error = nullptr);
void SetPluginEnabledState(AppRuntime &runtime, const std::string &config_path, bool enabled);
void RemovePluginEnabledState(AppRuntime &runtime, const std::string &config_path);

bool LoadPluginConfigFields(const std::string &config_path,
                            std::string *out_model_id,
                            std::string *out_model_version,
                            std::string *out_onnx,
                            std::vector<std::string> *out_extra_onnx,
                            std::string *out_error = nullptr);

bool SavePluginConfigFields(const std::string &config_path,
                            const std::string &model_id,
                            const std::string &onnx,
                            const std::vector<std::string> &extra_onnx,
                            std::string *out_error = nullptr);

void RefreshAsrProviders(AppRuntime &runtime);
bool SwitchAsrProviderByName(AppRuntime &runtime, const std::string &name, std::string *out_error = nullptr);

void RefreshOcrModels(AppRuntime &runtime);
bool SwitchOcrModelByName(AppRuntime &runtime, const std::string &name, std::string *out_error = nullptr);

void RefreshUnifiedPlugins(AppRuntime &runtime);
bool SwitchUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error = nullptr);
bool DeleteUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error = nullptr);

bool ReplaceUnifiedPluginAssets(AppRuntime &runtime,
                                const std::string &id,
                                const PluginAssetOverride &override,
                                std::string *out_error = nullptr);

bool CreateUserPlugin(AppRuntime &runtime, const UserPluginCreateRequest &request, std::string *out_error = nullptr);
void AppendPluginLog(AppRuntime &runtime,
                     const std::string &id,
                     PluginLogLevel level,
                     const std::string &message,
                     int error_code = 0);

bool ApplyOverrideModels(AppRuntime &runtime, std::string *out_error = nullptr);

}  // namespace desktoper2D
