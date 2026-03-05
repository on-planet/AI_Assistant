#pragma once

#include "k2d/controllers/param_controller.h"

namespace k2d {

struct AppRuntime;

void SetClickThrough(AppRuntime &runtime, bool enabled);
void ToggleWindowVisibility(AppRuntime &runtime);

ParamControllerContext BuildParamControllerContext(AppRuntime &runtime);

void SyncAnimationChannelState(AppRuntime &runtime);

bool HasModelParams(AppRuntime &runtime);
void EnsureSelectedParamIndexValid(AppRuntime &runtime);
void CycleSelectedParam(AppRuntime &runtime, int delta);
void AdjustSelectedParam(AppRuntime &runtime, float delta);
void ResetSelectedParam(AppRuntime &runtime);
void ResetAllParams(AppRuntime &runtime);
void ToggleManualParamMode(AppRuntime &runtime);

bool HasModelParts(const AppRuntime &runtime);
void EnsureSelectedPartIndexValid(AppRuntime &runtime);

}  // namespace k2d
