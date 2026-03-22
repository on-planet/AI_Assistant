#pragma once

#include <string>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

bool SwitchPluginByName(AppRuntime &runtime, const std::string &name, std::string *out_error = nullptr);
bool ReloadPluginByConfigPath(AppRuntime &runtime, const std::string &config_path, std::string *out_error = nullptr);
bool SwitchAsrProviderByName(AppRuntime &runtime, const std::string &name, std::string *out_error = nullptr);
bool SwitchOcrModelByName(AppRuntime &runtime, const std::string &name, std::string *out_error = nullptr);
bool SwitchUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error = nullptr);

}  // namespace desktoper2D
