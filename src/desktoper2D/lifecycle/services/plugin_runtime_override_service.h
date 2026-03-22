#pragma once

#include <string>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

enum class PerceptionReloadTarget {
    All,
    Asr,
    Scene,
    Ocr,
    Facemesh,
};

bool ReplaceUnifiedPluginAssets(AppRuntime &runtime,
                                const std::string &id,
                                const PluginAssetOverride &override,
                                std::string *out_error = nullptr);
bool ApplyOverrideModels(AppRuntime &runtime, std::string *out_error = nullptr);
bool ApplyOverrideModels(AppRuntime &runtime,
                         PerceptionReloadTarget target,
                         std::string *out_error = nullptr);

}  // namespace desktoper2D
