#pragma once

#include <string>

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/perception_pipeline.h"
#include "desktoper2D/lifecycle/state/task_category_types.h"

namespace desktoper2D::task_category_internal {

void InferTaskCategoryDetailed(const SystemContextSnapshot &ctx,
                               const OcrResult &ocr,
                               const SceneClassificationResult &scene,
                               const TaskCategoryConfig &config,
                               TaskCategoryInferenceState &inout_state,
                               const std::string *asr_session_text,
                               TaskCategoryInferenceResult &out_result,
                               RuntimeErrorInfo *out_decision_error);

}  // namespace desktoper2D::task_category_internal
