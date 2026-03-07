#pragma once

#include <string>

#include "k2d/lifecycle/observability/runtime_error_codes.h"
#include "k2d/lifecycle/perception_pipeline.h"
#include "k2d/lifecycle/state/task_category_types.h"

namespace k2d {

// 决策组件：将“任务决策”入口从运行时 Tick 解耦，便于独立测试。
void ComputeTaskDecision(const SystemContextSnapshot &ctx,
                         const OcrResult &ocr,
                         const SceneClassificationResult &scene,
                         const TaskCategoryConfig &config,
                         const std::string *asr_session_text,
                         TaskCategoryInferenceResult &out_result,
                         RuntimeErrorInfo *out_decision_error);

}  // namespace k2d
