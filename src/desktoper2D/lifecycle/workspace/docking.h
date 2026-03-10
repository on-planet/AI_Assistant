#pragma once

#include <string>

#include "imgui.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

#if defined(IMGUI_HAS_DOCK)
void LoadWorkspaceDockingIni(const std::string &workspace_docking_ini);
std::string SaveWorkspaceDockingIni();
bool IsWorkspaceDockTransactionActive();
void ApplyWorkspacePresetVisibility(AppRuntime &runtime);
void ApplyWorkspaceDockLayout(AppRuntime &runtime, ImGuiID dockspace_id);
#endif

}  // namespace desktoper2D
