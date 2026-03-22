#include "desktoper2D/lifecycle/services/task_category_decision.h"

#include "desktoper2D/lifecycle/services/task_category_features.h"
#include "desktoper2D/lifecycle/services/task_category_scoring.h"
#include "desktoper2D/lifecycle/services/task_category_service.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"

namespace desktoper2D {

const char *TaskPrimaryCategoryName(TaskPrimaryCategory c) {
    switch (c) {
        case TaskPrimaryCategory::Work: return "work";
        case TaskPrimaryCategory::Game: return "game";
        default: return "unknown";
    }
}

const char *TaskSecondaryCategoryName(TaskSecondaryCategory c) {
    switch (c) {
        case TaskSecondaryCategory::WorkCoding: return "coding";
        case TaskSecondaryCategory::WorkDebugging: return "debugging";
        case TaskSecondaryCategory::WorkReadingDocs: return "reading_docs";
        case TaskSecondaryCategory::WorkMeetingNotes: return "meeting_notes";
        case TaskSecondaryCategory::GameLobby: return "lobby";
        case TaskSecondaryCategory::GameMatch: return "match";
        case TaskSecondaryCategory::GameSettlement: return "settlement";
        case TaskSecondaryCategory::GameMenu: return "menu";
        default: return "unknown";
    }
}

namespace task_category_internal {

namespace {

float ComputePrimaryMemoryBoost(const TaskCategoryMemoryState &mem,
                                const TaskCategoryMemoryConfig &config,
                                TaskPrimaryCategory category) {
    if (category == TaskPrimaryCategory::Unknown || mem.primary_hist.empty()) {
        return 0.0f;
    }

    int same = 0;
    for (TaskPrimaryCategory value : mem.primary_hist) {
        if (value == category) {
            ++same;
        }
    }
    return std::clamp(static_cast<float>(same) / static_cast<float>(mem.primary_hist.size()) * config.primary_boost_max,
                      0.0f,
                      config.primary_boost_max);
}

float ComputeSecondaryMemoryBoost(const TaskCategoryMemoryState &mem,
                                  const TaskCategoryMemoryConfig &config,
                                  TaskSecondaryCategory category) {
    if (category == TaskSecondaryCategory::Unknown || mem.secondary_hist.empty()) {
        return 0.0f;
    }

    int same = 0;
    for (TaskSecondaryCategory value : mem.secondary_hist) {
        if (value == category) {
            ++same;
        }
    }
    return std::clamp(static_cast<float>(same) / static_cast<float>(mem.secondary_hist.size()) * config.secondary_boost_max,
                      0.0f,
                      config.secondary_boost_max);
}

void UpdateMemoryCache(TaskCategoryMemoryState &mem, const TaskCategoryInferenceResult &result) {
    if (mem.primary_hist.size() >= mem.max_hist) {
        mem.primary_hist.pop_front();
    }
    if (mem.secondary_hist.size() >= mem.max_hist) {
        mem.secondary_hist.pop_front();
    }
    mem.primary_hist.push_back(result.primary);
    mem.secondary_hist.push_back(result.secondary);
}

void ApplyTemporalSmoothing(TaskCategoryInferenceResult &result,
                            TaskCategoryTemporalState &state,
                            const TaskCategoryTemporalConfig &config) {
    if (!state.initialized) {
        state.initialized = true;
        state.primary = result.primary;
        state.secondary = result.secondary;
        state.primary_conf_ema = std::clamp(result.primary_confidence, 0.0f, 1.0f);
        state.secondary_conf_ema = std::clamp(result.secondary_confidence, 0.0f, 1.0f);
        state.primary_stable_frames = 1;
        state.secondary_stable_frames = 1;
        return;
    }

    const float raw_primary_conf = std::clamp(result.primary_confidence, 0.0f, 1.0f);
    const float raw_secondary_conf = std::clamp(result.secondary_confidence, 0.0f, 1.0f);

    state.primary_conf_ema = state.primary_conf_ema * (1.0f - config.ema_alpha) + raw_primary_conf * config.ema_alpha;
    state.secondary_conf_ema = state.secondary_conf_ema * (1.0f - config.ema_alpha) + raw_secondary_conf * config.ema_alpha;

    if (result.primary == TaskPrimaryCategory::Unknown &&
        raw_primary_conf <= config.unknown_primary_keep_threshold) {
        state.primary = TaskPrimaryCategory::Unknown;
        state.secondary = TaskSecondaryCategory::Unknown;
        state.primary_stable_frames = 1;
        state.secondary_stable_frames = 1;
    } else if (result.primary != state.primary) {
        const bool allow_switch =
            state.primary_stable_frames >= config.min_hold_frames &&
            raw_primary_conf >= (state.primary_conf_ema + config.switch_margin);
        if (allow_switch) {
            state.primary = result.primary;
            state.primary_stable_frames = 1;
            state.secondary = result.secondary;
            state.secondary_stable_frames = 1;
        } else {
            result.primary = state.primary;
            state.primary_stable_frames += 1;
        }
    } else {
        state.primary_stable_frames += 1;
    }

    if (result.secondary == TaskSecondaryCategory::Unknown &&
        raw_secondary_conf <= config.unknown_secondary_keep_threshold) {
        state.secondary = TaskSecondaryCategory::Unknown;
        state.secondary_stable_frames = 1;
    } else if (result.secondary != state.secondary) {
        const bool allow_switch =
            state.secondary_stable_frames >= config.min_hold_frames &&
            raw_secondary_conf >= (state.secondary_conf_ema + config.switch_margin);
        if (allow_switch) {
            state.secondary = result.secondary;
            state.secondary_stable_frames = 1;
        } else {
            result.secondary = state.secondary;
            state.secondary_stable_frames += 1;
        }
    } else {
        state.secondary_stable_frames += 1;
    }

    result.primary = state.primary;
    result.secondary = state.secondary;
    result.primary_confidence = state.primary_conf_ema;
    result.secondary_confidence = state.secondary_conf_ema;
}

}  // namespace

void InferTaskCategoryDetailed(const SystemContextSnapshot &ctx,
                               const OcrResult &ocr,
                               const SceneClassificationResult &scene,
                               const TaskCategoryConfig &config,
                               TaskCategoryInferenceState &inout_state,
                               const std::string *asr_session_text,
                               TaskCategoryInferenceResult &out_result,
                               RuntimeErrorInfo *out_decision_error) {
    const TaskCategoryCompiledConfig &compiled = EnsureCompiledConfig(config, inout_state);
    const TaskCategoryFeatureSnapshot features =
        BuildTaskCategoryFeatureSnapshot(ctx, ocr, scene, config, compiled, asr_session_text);
    const DynamicSourceWeights &w = features.source_weights;
    const PrimaryInferenceResult primary_result = InferPrimaryCategoryHierarchical(features, config);

    out_result = TaskCategoryInferenceResult{};
    out_result.primary = primary_result.primary;
    out_result.primary_confidence = primary_result.confidence;
    out_result.primary_structured_confidence = primary_result.structured_confidence;
    out_result.source_scene_weight = w.scene;
    out_result.source_ocr_weight = w.ocr;
    out_result.source_context_weight = w.context;
    out_result.scene_confidence = std::clamp(scene.score, 0.0f, 1.0f);

    enum class DecisionState {
        Idle,
        RejectPrimary,
        JointInfer,
        RejectSecondary,
        Accept,
    };

    DecisionState state = DecisionState::Idle;
    DecisionState memory_state = DecisionState::Idle;

    const float source_peak = std::max(w.scene, std::max(w.ocr, w.context));
    const bool weak_sources = source_peak < config.decision.min_reliable_source_weight;
    out_result.primary_confidence = std::clamp(
        out_result.primary_confidence + ComputePrimaryMemoryBoost(inout_state.memory, config.memory, out_result.primary),
        0.0f,
        1.0f);

    inout_state.decision.primary_conf_ema =
        inout_state.decision.primary_conf_ema * (1.0f - config.decision.decision_alpha) +
        out_result.primary_confidence * config.decision.decision_alpha;

    const bool primary_reject_now =
        out_result.primary_confidence < config.decision.primary_reject_threshold ||
        (weak_sources && out_result.primary_confidence < config.decision.weak_source_primary_reject_threshold);
    const bool primary_reject_mem =
        inout_state.decision.last_state == TaskCategoryDecisionMemory::StateTag::RejectPrimary &&
        inout_state.decision.hold_frames < config.decision.reject_hold_frames &&
        inout_state.decision.primary_conf_ema <
            (config.decision.primary_reject_threshold + config.decision.primary_reject_ema_margin);

    const bool primary_reject = primary_reject_now || primary_reject_mem;
    const bool primary_preempt =
        inout_state.decision.locked_primary != TaskPrimaryCategory::Unknown &&
        out_result.primary != inout_state.decision.locked_primary &&
        out_result.primary_confidence >= config.decision.primary_preempt_threshold &&
        (out_result.primary_confidence - inout_state.decision.primary_conf_ema) >=
            config.decision.primary_preempt_margin;

    state = (out_result.primary_confidence < config.decision.primary_consistency_threshold ||
             (primary_reject && !primary_preempt))
                ? DecisionState::RejectPrimary
                : DecisionState::JointInfer;
    memory_state = state;

    if (state == DecisionState::RejectPrimary) {
        out_result.primary = TaskPrimaryCategory::Unknown;
        out_result.primary_confidence = std::min(out_result.primary_confidence, config.decision.primary_reject_threshold);
        out_result.secondary = TaskSecondaryCategory::Unknown;
        out_result.secondary_confidence = 0.0f;
        out_result.secondary_structured_confidence = 0.0f;
        out_result.secondary_top_candidates.clear();
        state = DecisionState::Accept;
        if (out_decision_error) {
            UpdateRuntimeDegrade(*out_decision_error,
                                 RuntimeErrorDomain::DecisionHub,
                                 RuntimeErrorCode::DataQualityDegraded,
                                 "decision.reject_primary");
        }
    }

    if (state == DecisionState::JointInfer) {
        const float z_max = std::max(primary_result.game_score, primary_result.work_score);
        const float e_game = std::exp(primary_result.game_score - z_max);
        const float e_work = std::exp(primary_result.work_score - z_max);
        const float e_sum = e_game + e_work;
        const float p_game = (e_sum > 1e-6f) ? (e_game / e_sum) : 0.5f;
        const float p_work = (e_sum > 1e-6f) ? (e_work / e_sum) : 0.5f;

        const TaskPrimaryCategory top_primary = (p_game >= p_work) ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
        const TaskPrimaryCategory alt_primary = (top_primary == TaskPrimaryCategory::Game)
                                                    ? TaskPrimaryCategory::Work
                                                    : TaskPrimaryCategory::Game;
        const float top_primary_prob = std::max(p_game, p_work);
        const float alt_primary_prob = std::min(p_game, p_work);
        const bool run_second_branch =
            alt_primary_prob >= 0.35f && (top_primary_prob - alt_primary_prob) <= 0.20f;

        const SecondaryInferenceResult top_secondary =
            InferSecondaryCategory(features, top_primary, compiled, config);
        SecondaryInferenceResult alt_secondary{};
        if (run_second_branch) {
            alt_secondary = InferSecondaryCategory(features, alt_primary, compiled, config);
        }

        const float top_joint = top_primary_prob * top_secondary.confidence;
        const float alt_joint = run_second_branch ? (alt_primary_prob * alt_secondary.confidence) : -1.0f;
        const bool pick_alt = run_second_branch && alt_joint > top_joint;
        const TaskPrimaryCategory selected_primary = pick_alt ? alt_primary : top_primary;
        const SecondaryInferenceResult &joint_sec = pick_alt ? alt_secondary : top_secondary;
        out_result.primary = selected_primary;
        out_result.primary_confidence = CalibrateScoreConfidence(std::max(p_game, p_work),
                                                                 config.decision.joint_primary_confidence_temperature,
                                                                 config.decision.joint_primary_confidence_platt_a,
                                                                 config.decision.joint_primary_confidence_platt_b);
        out_result.secondary = joint_sec.category;
        out_result.secondary_confidence = CalibrateScoreConfidence(std::max(top_joint, alt_joint),
                                                                   config.decision.joint_secondary_confidence_temperature,
                                                                   config.decision.joint_secondary_confidence_platt_a,
                                                                   config.decision.joint_secondary_confidence_platt_b);
        out_result.secondary_structured_confidence = joint_sec.structured_confidence;

        std::vector<TaskCategorySecondaryCandidate> merged;
        merged.reserve(top_secondary.top_candidates.size() +
                       (run_second_branch ? alt_secondary.top_candidates.size() : 0));
        for (const auto &candidate : top_secondary.top_candidates) {
            merged.push_back(TaskCategorySecondaryCandidate{
                .category = candidate.category,
                .score = candidate.score * top_primary_prob,
            });
        }
        if (run_second_branch) {
            for (const auto &candidate : alt_secondary.top_candidates) {
                merged.push_back(TaskCategorySecondaryCandidate{
                    .category = candidate.category,
                    .score = candidate.score * alt_primary_prob,
                });
            }
        }
        std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
        out_result.secondary_top_candidates.clear();
        for (std::size_t i = 0; i < merged.size() && i < 3; ++i) {
            out_result.secondary_top_candidates.push_back(merged[i]);
        }

        out_result.secondary_confidence = std::clamp(
            out_result.secondary_confidence +
                ComputeSecondaryMemoryBoost(inout_state.memory, config.memory, out_result.secondary),
            0.0f,
            1.0f);

        inout_state.decision.secondary_conf_ema =
            inout_state.decision.secondary_conf_ema * (1.0f - config.decision.decision_alpha) +
            out_result.secondary_confidence * config.decision.decision_alpha;

        const bool secondary_reject_now =
            out_result.secondary == TaskSecondaryCategory::Unknown ||
            out_result.secondary_confidence < config.decision.secondary_reject_threshold ||
            (weak_sources && out_result.secondary_confidence < config.decision.weak_source_secondary_reject_threshold);
        const bool secondary_reject_mem =
            inout_state.decision.last_state == TaskCategoryDecisionMemory::StateTag::RejectSecondary &&
            inout_state.decision.hold_frames < config.decision.secondary_reject_hold_frames &&
            inout_state.decision.secondary_conf_ema <
                (config.decision.secondary_reject_threshold + config.decision.secondary_reject_ema_margin);

        const bool secondary_preempt =
            inout_state.decision.locked_secondary != TaskSecondaryCategory::Unknown &&
            out_result.secondary != inout_state.decision.locked_secondary &&
            out_result.secondary_confidence >= config.decision.secondary_preempt_threshold &&
            (out_result.secondary_confidence - inout_state.decision.secondary_conf_ema) >=
                config.decision.secondary_preempt_margin;

        if ((secondary_reject_now || secondary_reject_mem) && !secondary_preempt) {
            state = DecisionState::RejectSecondary;
        } else {
            state = DecisionState::Accept;
        }
        memory_state = state;
    }

    if (state == DecisionState::RejectSecondary) {
        out_result.secondary = TaskSecondaryCategory::Unknown;
        out_result.secondary_confidence = std::min(out_result.secondary_confidence, config.decision.secondary_reject_threshold);
        state = DecisionState::Accept;
    }

    if (state == DecisionState::Accept) {
        ApplyTemporalSmoothing(out_result, inout_state.temporal, config.temporal);
        UpdateMemoryCache(inout_state.memory, out_result);
    }

    auto to_tag = [](DecisionState value) {
        switch (value) {
            case DecisionState::RejectPrimary: return TaskCategoryDecisionMemory::StateTag::RejectPrimary;
            case DecisionState::JointInfer: return TaskCategoryDecisionMemory::StateTag::JointInfer;
            case DecisionState::RejectSecondary: return TaskCategoryDecisionMemory::StateTag::RejectSecondary;
            case DecisionState::Accept: return TaskCategoryDecisionMemory::StateTag::Accept;
            case DecisionState::Idle:
            default: return TaskCategoryDecisionMemory::StateTag::None;
        }
    };
    const TaskCategoryDecisionMemory::StateTag current_tag = to_tag(memory_state);
    if (current_tag == inout_state.decision.last_state) {
        inout_state.decision.hold_frames += 1;
    } else {
        inout_state.decision.last_state = current_tag;
        inout_state.decision.hold_frames = 1;
    }

    if (state == DecisionState::Accept) {
        inout_state.decision.locked_primary = out_result.primary;
        inout_state.decision.locked_secondary = out_result.secondary;
    }
}

}  // namespace desktoper2D::task_category_internal
}  // namespace desktoper2D
