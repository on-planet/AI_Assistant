#pragma once

#include <string>
#include <vector>

namespace k2d {

struct AppRuntime;

// UI 层：集中承载运行时调试与编辑面板渲染（可逐步迁移）
void RenderRuntimeDebugSummary(const AppRuntime &runtime);
void RenderWorkspaceToolbar(AppRuntime &runtime);

void RenderRuntimeOverviewPanel(AppRuntime &runtime);
void RenderRuntimeEditorPanel(AppRuntime &runtime);
void RenderRuntimeEditorParamGroups(AppRuntime &runtime, const std::vector<int> &param_indices);
void RenderRuntimeEditorBatchBind(AppRuntime &runtime, const std::string &group_label, const std::vector<int> &param_indices);
void RenderRuntimeTimelinePanel(AppRuntime &runtime);
void RenderRuntimePerceptionPanel(AppRuntime &runtime);
void RenderRuntimeMappingPanel(AppRuntime &runtime);
void RenderRuntimeAsrChatPanel(AppRuntime &runtime);
void RenderRuntimeErrorPanel(AppRuntime &runtime);
void RenderRuntimeOpsPanel(AppRuntime &runtime);

}  // namespace k2d
