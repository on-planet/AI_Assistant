#pragma once

#include "desktoper2D/lifecycle/state/runtime_state_slices.h"

namespace desktoper2D {

void TickAppSystems(PerceptionStateSlice perception, PluginStateSlice plugin, OpsStateSlice ops, float dt);
void ShutdownAppSystems(OpsStateSlice ops) noexcept;

}  // namespace desktoper2D
