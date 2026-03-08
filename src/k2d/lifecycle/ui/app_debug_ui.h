#pragma once

namespace k2d {

struct AppRuntime;

// UI 层：集中承载运行时调试与编辑面板渲染（可逐步迁移）
void RenderAppDebugUi(AppRuntime &runtime);
void RenderRuntimeDebugSummary(const AppRuntime &runtime);
void RenderWorkspaceToolbar(AppRuntime &runtime);

}  // namespace k2d
