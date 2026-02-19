#pragma once

#include "k2d/core/model.h"

namespace k2d {

struct ParamControllerContext {
    bool *model_loaded = nullptr;
    bool *manual_param_mode = nullptr;
    int *selected_param_index = nullptr;
    ModelRuntime *model = nullptr;
};

bool HasModelParams(const ParamControllerContext &ctx);
void EnsureSelectedParamIndexValid(const ParamControllerContext &ctx);
void CycleSelectedParam(const ParamControllerContext &ctx, int delta);
void AdjustSelectedParam(const ParamControllerContext &ctx, float delta);
void ResetSelectedParam(const ParamControllerContext &ctx);
void ResetAllParams(const ParamControllerContext &ctx);
void ToggleManualParamMode(const ParamControllerContext &ctx);

}  // namespace k2d

