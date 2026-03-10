#include "desktoper2D/lifecycle/plugin_postprocess.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace desktoper2D {

PostprocessTrace ExecutePostprocessPipeline(const std::vector<float> &input,
                                            const PluginPostprocessConfig &cfg) {
    PostprocessTrace trace{};
    trace.values = input;

    for (const auto &step : cfg.steps) {
        switch (step.type) {
            case PluginPostprocessStepType::Normalize: {
                float sum = 0.0f;
                for (float v : trace.values) sum += std::fabs(v);
                const float denom = std::max(step.epsilon, sum);
                if (denom > 0.0f) {
                    for (float &v : trace.values) v /= denom;
                }
                break;
            }
            case PluginPostprocessStepType::Tokenize: {
                trace.tokens.clear();
                const std::string delim = step.delimiter.empty() ? std::string(" ") : step.delimiter;
                for (float v : trace.values) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(3) << v;
                    std::string packed = oss.str();
                    std::size_t pos = 0;
                    while (true) {
                        const std::size_t hit = packed.find(delim, pos);
                        if (hit == std::string::npos) {
                            if (pos < packed.size()) trace.tokens.push_back(packed.substr(pos));
                            break;
                        }
                        if (hit > pos) trace.tokens.push_back(packed.substr(pos, hit - pos));
                        pos = hit + delim.size();
                    }
                    if (trace.tokens.empty()) trace.tokens.push_back(packed);
                }
                break;
            }
            case PluginPostprocessStepType::Argmax: {
                if (trace.values.empty()) {
                    trace.argmax_index = -1;
                    trace.argmax_value = 0.0f;
                    break;
                }
                int best_i = 0;
                float best_v = trace.values[0];
                for (int i = 1; i < static_cast<int>(trace.values.size()); ++i) {
                    if (trace.values[static_cast<std::size_t>(i)] > best_v) {
                        best_v = trace.values[static_cast<std::size_t>(i)];
                        best_i = i;
                    }
                }
                trace.argmax_index = best_i;
                trace.argmax_value = best_v;
                break;
            }
            case PluginPostprocessStepType::Threshold: {
                trace.has_threshold = true;
                trace.threshold_pass = trace.argmax_value >= step.threshold;
                break;
            }
            default:
                break;
        }
    }

    return trace;
}

}  // namespace desktoper2D
