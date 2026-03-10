#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/plugin_lifecycle.h"

namespace desktoper2D {

struct PostprocessTrace {
    std::vector<float> values;
    std::vector<std::string> tokens;
    int argmax_index = -1;
    float argmax_value = 0.0f;
    bool threshold_pass = false;
    bool has_threshold = false;
};

PostprocessTrace ExecutePostprocessPipeline(const std::vector<float> &input,
                                            const PluginPostprocessConfig &cfg);

}  // namespace desktoper2D
