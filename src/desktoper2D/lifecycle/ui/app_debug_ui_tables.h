#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/ui/app_debug_ui_presenter.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"

namespace desktoper2D {

void RenderModuleLatencyPanel(const AppRuntime &runtime);
void RenderRuntimeErrorClassificationTable(const AppRuntime &runtime, ErrorViewFilter filter);

}  // namespace desktoper2D
