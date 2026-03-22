#include "desktoper2D/lifecycle/state/runtime_state_ops.h"

#include "desktoper2D/controllers/window_controller.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void SetClickThrough(AppRuntime &runtime, bool enabled) {
    desktoper2D::SetClickThrough(runtime.window_state, enabled);
}

void ToggleWindowVisibility(AppRuntime &runtime) {
    desktoper2D::ToggleWindowVisibility(runtime.window_state);
}

ParamControllerContext BuildParamControllerContext(AppRuntime &runtime) {
    return ParamControllerContext{
        .model_loaded = &runtime.model_loaded,
        .manual_param_mode = &runtime.manual_param_mode,
        .selected_param_index = &runtime.selected_param_index,
        .model = &runtime.model,
    };
}

void SyncAnimationChannelState(AppRuntime &runtime) {
    if (!runtime.model_loaded) {
        return;
    }
    runtime.model.animation_channels_enabled = !runtime.manual_param_mode;
}

bool HasModelParams(AppRuntime &runtime) {
    return desktoper2D::HasModelParams(BuildParamControllerContext(runtime));
}

void EnsureSelectedParamIndexValid(AppRuntime &runtime) {
    desktoper2D::EnsureSelectedParamIndexValid(BuildParamControllerContext(runtime));
}

void CycleSelectedParam(AppRuntime &runtime, int delta) {
    desktoper2D::CycleSelectedParam(BuildParamControllerContext(runtime), delta);
}

void AdjustSelectedParam(AppRuntime &runtime, float delta) {
    desktoper2D::AdjustSelectedParam(BuildParamControllerContext(runtime), delta);
}

void ResetSelectedParam(AppRuntime &runtime) {
    desktoper2D::ResetSelectedParam(BuildParamControllerContext(runtime));
}

void ResetAllParams(AppRuntime &runtime) {
    desktoper2D::ResetAllParams(BuildParamControllerContext(runtime));
}

void ToggleManualParamMode(AppRuntime &runtime) {
    desktoper2D::ToggleManualParamMode(BuildParamControllerContext(runtime));
}

bool HasModelParts(const AppRuntime &runtime) {
    return runtime.model_loaded && !runtime.model.parts.empty();
}

void EnsureSelectedPartIndexValid(AppRuntime &runtime) {
    if (!(runtime.model_loaded && !runtime.model.parts.empty())) {
        runtime.selected_part_index = -1;
        return;
    }
    const int count = static_cast<int>(runtime.model.parts.size());
    if (runtime.selected_part_index < 0 || runtime.selected_part_index >= count) {
        runtime.selected_part_index = 0;
    }
}

}  // namespace desktoper2D
