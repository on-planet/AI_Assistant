#pragma once

#include "k2d/core/model.h"

namespace k2d {

struct AppRuntime;

void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y);
void UndoLastEdit(AppRuntime &runtime);
void RedoLastEdit(AppRuntime &runtime);
void SaveEditedModelJsonToDisk(AppRuntime &runtime);

}  // namespace k2d
