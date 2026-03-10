#include "desktoper2D/lifecycle/services/decision_service.h"

#include "desktoper2D/lifecycle/services/task_category_service.h"

namespace desktoper2D {

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

}  // namespace desktoper2D
