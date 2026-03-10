#pragma once

#include "desktoper2D/lifecycle/bridge_factory.h"
#include "desktoper2D/lifecycle/model_reload_service.h"

namespace desktoper2D {

ModelReloadServiceContext BuildModelReloadServiceContext(AppRuntime &runtime);
BehaviorApplyContext BuildBehaviorApplyContext(AppRuntime &runtime);
EditorInputBindingBridge BuildEditorInputBindingBridge(AppRuntime &runtime);
AppEventHandlerBridge BuildAppEventHandlerBridge(AppRuntime &runtime);
RuntimeTickBridge BuildRuntimeTickBridge(AppRuntime &runtime);
RuntimeRenderBridge BuildRuntimeRenderBridge(AppRuntime &runtime);
EditorInputCallbacks BuildEditorInputCallbacks(AppRuntime &runtime);

}  // namespace desktoper2D
