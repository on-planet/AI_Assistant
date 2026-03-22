#pragma once

#include <string>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void RefreshAsrProviders(AppRuntime &runtime);
void RefreshOcrModels(AppRuntime &runtime);
void RefreshDefaultPluginCatalog(AppRuntime &runtime);
void RefreshUnifiedPlugins(AppRuntime &runtime);
bool DeleteUnifiedPluginById(AppRuntime &runtime, const std::string &id, std::string *out_error = nullptr);

}  // namespace desktoper2D
