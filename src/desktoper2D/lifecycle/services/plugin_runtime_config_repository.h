#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

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

void RefreshPluginConfigs(AppRuntime &runtime);
bool DeletePluginConfig(AppRuntime &runtime, const std::string &config_path, std::string *out_error = nullptr);
void SetPluginEnabledState(AppRuntime &runtime, const std::string &config_path, bool enabled);
void RemovePluginEnabledState(AppRuntime &runtime, const std::string &config_path);
bool CreateUserPlugin(AppRuntime &runtime, const UserPluginCreateRequest &request, std::string *out_error = nullptr);

}  // namespace desktoper2D
