#pragma once

#include <string>

#include "desktoper2D/core/json.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_types.h"

namespace desktoper2D {

void ResetPerceptionRuntimeState(PerceptionPipelineState &state);
void ResetAllRuntimeErrorCounters(AppRuntime &runtime);
bool ExportRuntimeSnapshotJson(const AppRuntime &runtime, const char *path, std::string *out_error);
void TriggerSingleStepSampling(AppRuntime &runtime);

}  // namespace desktoper2D
