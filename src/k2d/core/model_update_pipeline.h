#pragma once

#include "model.h"

namespace k2d {

void ApplyAnimationChannelTargets(ModelRuntime *model, float time_sec);
void UpdateParameterValues(ModelRuntime *model, float dt_sec);
bool UpdateDirtyCachesAndCheckChanged(ModelRuntime *model);
void ResolveLocalPartStates(ModelRuntime *model, float time_sec, float dt_sec);
void ResolveWorldTransforms(ModelRuntime *model);
void ApplyWorldDeforms(ModelRuntime *model, float dt_sec);

}  // namespace k2d
