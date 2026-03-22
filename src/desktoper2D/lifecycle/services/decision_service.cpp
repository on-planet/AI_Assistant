#include "desktoper2D/lifecycle/services/decision_service.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string_view>

#include "desktoper2D/lifecycle/services/task_category_decision.h"

namespace desktoper2D {

namespace {

std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

std::uint64_t HashString(std::string_view text) {
    return static_cast<std::uint64_t>(std::hash<std::string_view>{}(text));
}

std::uint64_t QuantizeFloat(float value, float scale) {
    const float clamped = std::clamp(value, -10000.0f, 10000.0f);
    return static_cast<std::uint64_t>(std::llround(clamped * scale));
}

bool ShouldUseBlackboardOcr(const PerceptionPipelineState &state) {
    return !state.blackboard.ocr.summary.empty() || !state.blackboard.ocr.lines.empty() ||
           !state.blackboard.ocr.domain_tags.empty();
}

}  // namespace

std::uint64_t HashTaskDecisionSystemContext(const SystemContextSnapshot &ctx) {
    std::uint64_t signature = 0;
    signature = HashCombine(signature, HashString(ctx.process_name));
    signature = HashCombine(signature, HashString(ctx.window_title));
    signature = HashCombine(signature, HashString(ctx.url_hint));
    return signature;
}

std::uint64_t HashTaskDecisionOcrResult(const OcrResult &ocr) {
    std::uint64_t signature = 0;
    signature = HashCombine(signature, HashString(ocr.summary));
    signature = HashCombine(signature, static_cast<std::uint64_t>(ocr.lines.size()));
    for (const auto &line : ocr.lines) {
        signature = HashCombine(signature, HashString(line.text));
        signature = HashCombine(signature, QuantizeFloat(line.score, 1000.0f));
    }
    return signature;
}

std::uint64_t HashTaskDecisionSceneResult(const SceneClassificationResult &scene) {
    std::uint64_t signature = 0;
    signature = HashCombine(signature, HashString(scene.label));
    signature = HashCombine(signature, QuantizeFloat(scene.score, 1000.0f));
    return signature;
}

std::uint64_t HashTaskDecisionAsrSessionText(std::string_view asr_session_text) {
    return HashString(asr_session_text);
}

std::uint64_t BuildTaskDecisionInputSignatureFromComponents(std::uint64_t context_signature,
                                                            std::uint64_t ocr_signature,
                                                            std::uint64_t scene_signature,
                                                            std::uint64_t asr_signature) {
    std::uint64_t signature = 0;
    signature = HashCombine(signature, context_signature);
    signature = HashCombine(signature, ocr_signature);
    signature = HashCombine(signature, scene_signature);
    signature = HashCombine(signature, asr_signature);
    return signature;
}

void ResetTaskDecisionInputCache(PerceptionPipelineState &state) {
    state.decision_signature = TaskDecisionSignatureState{};
}

const OcrResult &GetTaskDecisionOcrInput(const PerceptionPipelineState &state) {
    return state.decision_signature.ocr_uses_blackboard ? state.decision_signature.cached_ocr_input
                                                        : state.ocr_result;
}

void RefreshTaskDecisionInputCache(PerceptionPipelineState &state) {
    auto &signature = state.decision_signature;

    if (signature.system_context_dirty) {
        signature.system_context_signature = HashTaskDecisionSystemContext(state.system_context_snapshot);
        signature.system_context_dirty = false;
        signature.input_dirty = true;
    }

    if (signature.ocr_dirty) {
        signature.ocr_uses_blackboard = ShouldUseBlackboardOcr(state);
        if (signature.ocr_uses_blackboard) {
            signature.cached_ocr_input.lines = state.blackboard.ocr.lines;
            signature.cached_ocr_input.summary = state.blackboard.ocr.summary;
            signature.cached_ocr_input.domain_tags = state.blackboard.ocr.domain_tags;
        } else {
            signature.cached_ocr_input = OcrResult{};
        }
        signature.ocr_signature = HashTaskDecisionOcrResult(GetTaskDecisionOcrInput(state));
        signature.ocr_dirty = false;
        signature.input_dirty = true;
    }

    if (signature.scene_dirty) {
        signature.scene_signature = HashTaskDecisionSceneResult(state.scene_result);
        signature.scene_dirty = false;
        signature.input_dirty = true;
    }

    if (signature.asr_dirty) {
        signature.asr_signature = HashTaskDecisionAsrSessionText(state.blackboard.asr.session_text);
        signature.asr_dirty = false;
        signature.input_dirty = true;
    }

    if (signature.input_dirty) {
        signature.input_signature = BuildTaskDecisionInputSignatureFromComponents(signature.system_context_signature,
                                                                                  signature.ocr_signature,
                                                                                  signature.scene_signature,
                                                                                  signature.asr_signature);
        signature.input_dirty = false;
    }
}

void PublishTaskDecisionSystemContext(PerceptionPipelineState &state) {
    state.decision_signature.system_context_dirty = true;
    RefreshTaskDecisionInputCache(state);
}

void PublishTaskDecisionOcrInput(PerceptionPipelineState &state) {
    state.decision_signature.ocr_dirty = true;
    RefreshTaskDecisionInputCache(state);
}

void PublishTaskDecisionSceneResult(PerceptionPipelineState &state) {
    state.decision_signature.scene_dirty = true;
    RefreshTaskDecisionInputCache(state);
}

void PublishTaskDecisionAsrSession(PerceptionPipelineState &state) {
    state.decision_signature.asr_dirty = true;
    RefreshTaskDecisionInputCache(state);
}

void ComputeTaskDecision(const SystemContextSnapshot &ctx,
                         const OcrResult &ocr,
                         const SceneClassificationResult &scene,
                         const TaskCategoryConfig &config,
                         TaskCategoryInferenceState &inout_state,
                         const std::string *asr_session_text,
                         TaskCategoryInferenceResult &out_result,
                         RuntimeErrorInfo *out_decision_error) {
    task_category_internal::InferTaskCategoryDetailed(ctx,
                                                      ocr,
                                                      scene,
                                                      config,
                                                      inout_state,
                                                      asr_session_text,
                                                      out_result,
                                                      out_decision_error);
}

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
                      RuntimeErrorInfo *out_decision_error) {
    inout_schedule.poll_accum_sec += std::max(0.0f, dt);

    const bool input_changed =
        !inout_schedule.has_last_input_signature ||
        inout_schedule.last_input_signature != input_signature;
    const bool interval_elapsed = inout_schedule.poll_accum_sec >= inout_schedule.min_interval_sec;

    if (!inout_schedule.force_refresh && !input_changed && !interval_elapsed) {
        inout_schedule.skipped_count += 1;
        return false;
    }

    ComputeTaskDecision(ctx,
                        ocr,
                        scene,
                        config,
                        inout_state,
                        asr_session_text,
                        inout_result,
                        out_decision_error);
    inout_schedule.poll_accum_sec = 0.0f;
    inout_schedule.last_input_signature = input_signature;
    inout_schedule.has_last_input_signature = true;
    inout_schedule.force_refresh = false;
    inout_schedule.compute_count += 1;
    return true;
}

}  // namespace desktoper2D
