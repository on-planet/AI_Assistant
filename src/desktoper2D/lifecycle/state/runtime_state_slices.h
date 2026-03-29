#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

struct EditorStateSlice {
    RuntimeCoreState &core;
    RuntimeEditorState &editor;
};

struct PerceptionStateSlice {
    RuntimeCoreState &core;
    RuntimePerceptionHostState &perception;
};

struct PluginStateSlice {
    RuntimeCoreState &core;
    RuntimeUiState &ui;
    RuntimePerceptionHostState &perception;
    RuntimePluginHostState &plugin;
    RuntimeObservabilityStateHost &observability;
};

struct WorkspaceStateSlice {
    RuntimeUiState &ui;
    RuntimeEditorState &editor;
};

struct OpsStateSlice {
    RuntimeCoreState &core;
    RuntimePerceptionHostState &perception;
    RuntimeServiceState &service;
    RuntimeAsrChatState &asr_chat;
    RuntimeObservabilityStateHost &observability;
};

struct RuntimeTickStateSlices {
    EditorStateSlice editor;
    PerceptionStateSlice perception;
    PluginStateSlice plugin;
    WorkspaceStateSlice workspace;
    OpsStateSlice ops;
};

inline EditorStateSlice BuildEditorStateSlice(AppRuntime &runtime) {
    return EditorStateSlice{
        .core = runtime,
        .editor = runtime,
    };
}

inline PerceptionStateSlice BuildPerceptionStateSlice(AppRuntime &runtime) {
    return PerceptionStateSlice{
        .core = runtime,
        .perception = runtime,
    };
}

inline PluginStateSlice BuildPluginStateSlice(AppRuntime &runtime) {
    return PluginStateSlice{
        .core = runtime,
        .ui = runtime,
        .perception = runtime,
        .plugin = runtime,
        .observability = runtime,
    };
}

inline WorkspaceStateSlice BuildWorkspaceStateSlice(AppRuntime &runtime) {
    return WorkspaceStateSlice{
        .ui = runtime,
        .editor = runtime,
    };
}

inline OpsStateSlice BuildOpsStateSlice(AppRuntime &runtime) {
    return OpsStateSlice{
        .core = runtime,
        .perception = runtime,
        .service = runtime,
        .asr_chat = runtime,
        .observability = runtime,
    };
}

inline RuntimeTickStateSlices BuildRuntimeTickStateSlices(AppRuntime &runtime) {
    return RuntimeTickStateSlices{
        .editor = BuildEditorStateSlice(runtime),
        .perception = BuildPerceptionStateSlice(runtime),
        .plugin = BuildPluginStateSlice(runtime),
        .workspace = BuildWorkspaceStateSlice(runtime),
        .ops = BuildOpsStateSlice(runtime),
    };
}

}  // namespace desktoper2D
