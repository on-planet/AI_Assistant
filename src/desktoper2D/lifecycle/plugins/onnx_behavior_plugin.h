#pragma once

#include "desktoper2D/lifecycle/plugin_lifecycle.h"

namespace desktoper2D {

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPlugin(const PluginArtifactSpec &spec);

std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPluginFromConfig(const std::string &config_path,
                                                                    std::string *out_error,
                                                                    PluginArtifactSpec *out_spec);

}  // namespace desktoper2D
