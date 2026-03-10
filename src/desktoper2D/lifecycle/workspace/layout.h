#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/workspace/types.h"

namespace desktoper2D {

const char *WorkspaceModeName(WorkspaceMode mode);
WorkspaceWindowVisibility BuildWorkspaceDefaultVisibility(WorkspaceMode mode);
void ApplyWorkspaceWindowVisibility(AppRuntime &runtime, const WorkspaceWindowVisibility &v);
bool HasAnyWorkspaceChildWindowVisible(const AppRuntime &runtime);

}  // namespace desktoper2D
