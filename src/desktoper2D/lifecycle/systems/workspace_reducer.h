#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void ReduceSwitchWorkspaceMode(AppRuntime &runtime, const UiCommand &cmd);
void ReduceApplyPresetLayout(AppRuntime &runtime, const UiCommand &cmd);
void ReduceResetManualLayout(AppRuntime &runtime, const UiCommand &cmd);
void ReduceToggleManualLayout(AppRuntime &runtime, const UiCommand &cmd);
void ReduceForceDockRebuild(AppRuntime &runtime, const UiCommand &cmd);

}  // namespace desktoper2D
