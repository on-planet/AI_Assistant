#pragma once

#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

void PushRuntimeEvent(AppRuntime &runtime, const RuntimeEvent &event);
void ExecuteUiCommand(AppRuntime &runtime, const UiCommand &cmd);
void ConsumeUiCommandQueue(AppRuntime &runtime);

}  // namespace k2d
