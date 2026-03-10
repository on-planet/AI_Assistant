#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void PushRuntimeEvent(AppRuntime &runtime, const RuntimeEvent &event);

}  // namespace desktoper2D
