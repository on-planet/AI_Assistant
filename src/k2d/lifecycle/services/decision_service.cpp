#include "k2d/lifecycle/services/decision_service.h"

#include "k2d/lifecycle/services/task_category_service.h"

namespace k2d {

void ComputeTaskDecision(const SystemContextSnapshot &ctx,
                         const OcrResult &ocr,
                         const SceneClassificationResult &scene,
                         const TaskCategoryConfig &config,
                         const std::string *asr_session_text,
                         TaskCategoryInferenceResult &out_result,
                         RuntimeErrorInfo *out_decision_error) {
    InferTaskCategoryDetailed(ctx,
                              ocr,
                              scene,
                              config,
                              asr_session_text,
                              out_result,
                              out_decision_error);
}

}  // namespace k2d
