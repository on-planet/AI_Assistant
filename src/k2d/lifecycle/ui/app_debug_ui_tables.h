#pragma once

#include <string>
#include <vector>

#include "k2d/lifecycle/ui/app_debug_ui_presenter.h"
#include "k2d/lifecycle/ui/app_debug_ui_widgets.h"

namespace k2d {

void RenderModuleLatencyPanel(const AppRuntime &runtime);
void RenderRuntimeErrorClassificationTable(const AppRuntime &runtime, ErrorViewFilter filter);

}  // namespace k2d
