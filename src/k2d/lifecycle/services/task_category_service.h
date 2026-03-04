#pragma once

#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/perception_pipeline.h"

namespace k2d {

const char *TaskPrimaryCategoryName(TaskPrimaryCategory c);
const char *TaskSecondaryCategoryName(TaskSecondaryCategory c);

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary);

}  // namespace k2d
