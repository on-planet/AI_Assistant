#include "k2d/lifecycle/state/runtime_state_ops.h"

#include "k2d/controllers/window_controller.h"
#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

void SetClickThrough(AppRuntime &runtime, bool enabled) {
    runtime.click_through = enabled;

    if (runtime.entry_click_through) {
        SDL_SetTrayEntryChecked(runtime.entry_click_through, enabled);
    }

    k2d::ApplyWindowShape(runtime.window, runtime.window_w, runtime.window_h, runtime.interactive_rect, enabled);
}

void ToggleWindowVisibility(AppRuntime &runtime) {
    k2d::ToggleWindowVisibility(runtime.window, &runtime.window_visible);
    k2d::UpdateWindowVisibilityLabel(runtime.entry_show_hide, runtime.window_visible);
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
    return k2d::HasModelParams(BuildParamControllerContext(runtime));
}

void EnsureSelectedParamIndexValid(AppRuntime &runtime) {
    k2d::EnsureSelectedParamIndexValid(BuildParamControllerContext(runtime));
}

void CycleSelectedParam(AppRuntime &runtime, int delta) {
    k2d::CycleSelectedParam(BuildParamControllerContext(runtime), delta);
}

void AdjustSelectedParam(AppRuntime &runtime, float delta) {
    k2d::AdjustSelectedParam(BuildParamControllerContext(runtime), delta);
}

void ResetSelectedParam(AppRuntime &runtime) {
    k2d::ResetSelectedParam(BuildParamControllerContext(runtime));
}

void ResetAllParams(AppRuntime &runtime) {
    k2d::ResetAllParams(BuildParamControllerContext(runtime));
}

void ToggleManualParamMode(AppRuntime &runtime) {
    k2d::ToggleManualParamMode(BuildParamControllerContext(runtime));
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

}  // namespace k2d
