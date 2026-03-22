#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/perception_pipeline.h"
#include "desktoper2D/lifecycle/state/task_category_types.h"

namespace desktoper2D {

std::uint64_t HashTaskDecisionSystemContext(const SystemContextSnapshot &ctx);
std::uint64_t HashTaskDecisionOcrResult(const OcrResult &ocr);
std::uint64_t HashTaskDecisionSceneResult(const SceneClassificationResult &scene);
std::uint64_t HashTaskDecisionAsrSessionText(std::string_view asr_session_text);
std::uint64_t BuildTaskDecisionInputSignatureFromComponents(std::uint64_t context_signature,
                                                            std::uint64_t ocr_signature,
                                                            std::uint64_t scene_signature,
                                                            std::uint64_t asr_signature);
void ResetTaskDecisionInputCache(PerceptionPipelineState &state);
void PublishTaskDecisionSystemContext(PerceptionPipelineState &state);
void PublishTaskDecisionOcrInput(PerceptionPipelineState &state);
void PublishTaskDecisionSceneResult(PerceptionPipelineState &state);
void PublishTaskDecisionAsrSession(PerceptionPipelineState &state);
void RefreshTaskDecisionInputCache(PerceptionPipelineState &state);
const OcrResult &GetTaskDecisionOcrInput(const PerceptionPipelineState &state);

// 决策组件：将“任务决策”入口从运行时 Tick 解耦，便于独立测试。
void ComputeTaskDecision(const SystemContextSnapshot &ctx,
                         const OcrResult &ocr,
                         const SceneClassificationResult &scene,
                         const TaskCategoryConfig &config,
                         TaskCategoryInferenceState &inout_state,
                         const std::string *asr_session_text,
                         TaskCategoryInferenceResult &out_result,
                         RuntimeErrorInfo *out_decision_error);

bool TickTaskDecision(const SystemContextSnapshot &ctx,
                      const OcrResult &ocr,
                      const SceneClassificationResult &scene,
                      std::uint64_t input_signature,
                      float dt,
                      const TaskCategoryConfig &config,
                      TaskCategoryScheduleState &inout_schedule,
                      TaskCategoryInferenceState &inout_state,
                      const std::string *asr_session_text,
                      TaskCategoryInferenceResult &inout_result,
                      RuntimeErrorInfo *out_decision_error);

}  // namespace desktoper2D
