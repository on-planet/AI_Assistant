#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

#include <string>
#include <vector>

namespace desktoper2D {

struct RuntimeUiView {
    AppRuntime &runtime;
    RuntimeCoreState &core;
    RuntimeUiState &ui;
    RuntimeEditorState &editor;
    RuntimeServiceState &service;
    RuntimePerceptionHostState &perception;
    RuntimeAsrChatState &asr_chat;
    RuntimePluginHostState &plugin;
    RuntimeObservabilityStateHost &observability;
};

inline RuntimeUiView BuildRuntimeUiView(AppRuntime &runtime) {
    return RuntimeUiView{
        .runtime = runtime,
        .core = runtime,
        .ui = runtime,
        .editor = runtime,
        .service = runtime,
        .perception = runtime,
        .asr_chat = runtime,
        .plugin = runtime,
        .observability = runtime,
    };
}

void RenderWorkspaceToolbar(RuntimeUiView view);

void RenderRuntimeOverviewPanel(RuntimeUiView view);
void RenderRuntimeEditorPanel(RuntimeUiView view);
void RenderRuntimeEditorParamGroups(RuntimeUiView view, const std::vector<int> &param_indices);
void RenderRuntimeEditorBatchBind(RuntimeUiView view,
                                  const std::string &group_label,
                                  const std::vector<int> &param_indices);
void RenderRuntimeTimelinePanel(RuntimeUiView view);
void RenderRuntimePerceptionPanel(RuntimeUiView view);
void RenderRuntimeMappingPanel(RuntimeUiView view);
void RenderRuntimeOcrPanel(RuntimeUiView view);
void RenderRuntimeAsrPanel(RuntimeUiView view);
void RenderRuntimePluginWorkerPanel(RuntimeUiView view);
void RenderRuntimeChatPanel(RuntimeUiView view);
void RenderRuntimeErrorPanel(RuntimeUiView view);
void RenderRuntimePluginQuickControlPanel(RuntimeUiView view);
void RenderRuntimePluginDetailPanel(RuntimeUiView view);
void RenderRuntimeOpsPanel(RuntimeUiView view);

}  // namespace desktoper2D
