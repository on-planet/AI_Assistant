#pragma once

#include "desktoper2D/lifecycle/ui/app_debug_ui_panel_state.h"

namespace desktoper2D {

void ApplyTimelinePanelActionImpl(AppRuntime &runtime, const TimelinePanelAction &action);
void ApplyEditorPanelActionImpl(AppRuntime &runtime, const EditorPanelAction &action);

}  // namespace desktoper2D
