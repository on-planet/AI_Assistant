#pragma once

#include "k2d/lifecycle/perception_pipeline.h"
#include "k2d/lifecycle/observability/runtime_error_codes.h"
#include "k2d/lifecycle/state/task_category_types.h"

namespace k2d {

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

}  // namespace k2d
