#pragma once

#include "desktoper2D/lifecycle/services/plugin_runtime_catalog_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_config_repository.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_override_service.h"
#include "desktoper2D/lifecycle/services/plugin_runtime_switch_service.h"

namespace desktoper2D {

// Plugin lifecycle facade: runtime init/shutdown plus shared logging entrypoints.
void UpdatePluginLifecycle(AppRuntime &runtime);
void AppendPluginLog(AppRuntime &runtime,
                     const std::string &id,
                     PluginLogLevel level,
                     const std::string &message,
                     int error_code = 0);

}  // namespace desktoper2D
