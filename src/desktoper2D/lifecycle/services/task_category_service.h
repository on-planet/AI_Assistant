#pragma once

#include "desktoper2D/lifecycle/perception_pipeline.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/state/task_category_types.h"

namespace desktoper2D {

const char *TaskPrimaryCategoryName(TaskPrimaryCategory c);
const char *TaskSecondaryCategoryName(TaskSecondaryCategory c);

void InferTaskCategoryDetailed(const SystemContextSnapshot &ctx,
                               const OcrResult &ocr,
                               const SceneClassificationResult &scene,
                               const TaskCategoryConfig &config,
                               const std::string *asr_session_text,
                               TaskCategoryInferenceResult &out_result,
                               RuntimeErrorInfo *out_decision_error);

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       const TaskCategoryConfig &config,
                       const std::string *asr_session_text,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary);

}  // namespace desktoper2D
