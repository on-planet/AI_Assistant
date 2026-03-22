#pragma once

#include "desktoper2D/lifecycle/state/runtime_window_state.h"

namespace desktoper2D {

bool InitRuntimeImGuiBackends(const RuntimeWindowState &window_state);
void ShutdownRuntimeImGuiBackends();
void BeginRuntimeImGuiFrame();
void RenderRuntimeImGuiDrawData(const RuntimeWindowState &window_state);

}  // namespace desktoper2D
