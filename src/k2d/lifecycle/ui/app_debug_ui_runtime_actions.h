#pragma once

#include <string>

#include "k2d/core/json.h"
#include "k2d/lifecycle/ui/app_debug_ui_types.h"

namespace k2d {

void ResetPerceptionRuntimeState(PerceptionPipelineState &state);
void ResetAllRuntimeErrorCounters(AppRuntime &runtime);
bool ExportRuntimeSnapshotJson(const AppRuntime &runtime, const char *path, std::string *out_error);
void TriggerSingleStepSampling(AppRuntime &runtime);

}  // namespace k2d
