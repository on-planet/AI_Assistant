#include "desktoper2D/lifecycle/services/task_category_scoring.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace desktoper2D::task_category_internal {

namespace {

TaskSecondaryCategory CanonicalizeSecondaryCategory(TaskSecondaryCategory category) {
    return category;
}

float LongTailClassBoost(TaskPrimaryCategory primary, TaskSecondaryCategory secondary) {
    if (primary == TaskPrimaryCategory::Work) {
        switch (secondary) {
            case TaskSecondaryCategory::WorkMeetingNotes: return 1.20f;
            case TaskSecondaryCategory::WorkReadingDocs: return 1.08f;
            case TaskSecondaryCategory::WorkDebugging: return 1.05f;
            default: return 1.00f;
        }
    }

    if (primary == TaskPrimaryCategory::Game) {
        switch (secondary) {
            case TaskSecondaryCategory::GameSettlement:
            case TaskSecondaryCategory::GameLobby:
            case TaskSecondaryCategory::GameMenu: return 1.15f;
            case TaskSecondaryCategory::GameMatch: return 0.92f;
            default: return 1.00f;
        }
    }

    return 1.00f;
}

float ComputeExplicitSignalAdjustment(TaskPrimaryCategory primary,
                                      TaskSecondaryCategory secondary,
                                      const ExplicitSentenceSignals &signals) {
    float neg_adjust = 0.0f;
    if (signals.negation > 0.0f) {
        if (secondary == TaskSecondaryCategory::WorkMeetingNotes ||
            secondary == TaskSecondaryCategory::GameLobby) {
            neg_adjust -= 0.32f * signals.negation;
        } else {
            neg_adjust -= 0.10f * signals.negation;
        }
    }

    float cond_adjust = 0.0f;
    if (signals.conditional > 0.0f) {
        if (secondary == TaskSecondaryCategory::WorkCoding ||
            secondary == TaskSecondaryCategory::GameMatch) {
            cond_adjust += 0.20f * signals.conditional;
        } else {
            cond_adjust += 0.06f * signals.conditional;
        }
    }

    float multi_adjust = 0.0f;
    if (signals.multi_intent > 0.0f) {
        if (primary == TaskPrimaryCategory::Work) {
            multi_adjust += 0.08f * signals.multi_intent;
        } else {
            multi_adjust += 0.05f * signals.multi_intent;
        }
    }

    return neg_adjust + cond_adjust + multi_adjust;
}

TaskPrimaryCategory InferPrimaryFromSingleSource(int game_semantic_hits,
                                                 int work_semantic_hits,
                                                 float game_structured,
                                                 float work_structured,
                                                 const TaskCategoryPrimaryConfig &config) {
    const float game_semantic = static_cast<float>(game_semantic_hits);
    const float work_semantic = static_cast<float>(work_semantic_hits);
    const float game_score =
        config.single_source_semantic_weight * game_semantic + config.single_source_structured_weight * game_structured;
    const float work_score =
        config.single_source_semantic_weight * work_semantic + config.single_source_structured_weight * work_structured;
    return (game_score >= work_score) ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
}

float ComputePrimarySourceConsistency(const TaskCategoryFeatureSnapshot &snapshot,
                                      const TaskCategoryPrimaryConfig &config) {
    std::vector<TaskPrimaryCategory> votes;
    if (!snapshot.scene_text.empty()) {
        votes.push_back(InferPrimaryFromSingleSource(snapshot.game_primary_hits[0],
                                                     snapshot.work_primary_hits[0],
                                                     snapshot.game_primary_structured_scores[0],
                                                     snapshot.work_primary_structured_scores[0],
                                                     config));
    }
    if (!snapshot.ocr_text.empty()) {
        votes.push_back(InferPrimaryFromSingleSource(snapshot.game_primary_hits[1],
                                                     snapshot.work_primary_hits[1],
                                                     snapshot.game_primary_structured_scores[1],
                                                     snapshot.work_primary_structured_scores[1],
                                                     config));
    }
    if (!snapshot.ctx_text.empty()) {
        votes.push_back(InferPrimaryFromSingleSource(snapshot.game_primary_hits[2],
                                                     snapshot.work_primary_hits[2],
                                                     snapshot.game_primary_structured_scores[2],
                                                     snapshot.work_primary_structured_scores[2],
                                                     config));
    }

    if (votes.empty()) {
        return 0.0f;
    }
    if (votes.size() == 1) {
        return 0.55f;
    }

    int game_votes = 0;
    int work_votes = 0;
    for (TaskPrimaryCategory category : votes) {
        if (category == TaskPrimaryCategory::Game) {
            ++game_votes;
        } else {
            ++work_votes;
        }
    }

    const int total = static_cast<int>(votes.size());
    const int top_votes = std::max(game_votes, work_votes);
    const float agreement_ratio = static_cast<float>(top_votes) / static_cast<float>(total);
    const bool top1_same = (top_votes == total);
    return std::clamp(0.65f * agreement_ratio + 0.35f * (top1_same ? 1.0f : 0.0f), 0.0f, 1.0f);
}

}  // namespace

SecondaryInferenceResult InferSecondaryCategory(const TaskCategoryFeatureSnapshot &snapshot,
                                                TaskPrimaryCategory primary,
                                                const TaskCategoryCompiledConfig &compiled,
                                                const TaskCategoryConfig &config) {
    SecondaryInferenceResult result{};

    const auto weighted_hits_from_cache = [&](std::size_t rule_idx) {
        float weighted_hits = 0.0f;
        for (std::size_t source_idx = 0; source_idx < snapshot.source_texts.size(); ++source_idx) {
            const auto &source = snapshot.source_texts[source_idx];
            if (!source.first || source.second <= 0.0f) {
                continue;
            }
            const int hits = snapshot.secondary_rule_hits[rule_idx][source_idx];
            if (hits > 0) {
                weighted_hits += static_cast<float>(hits) * source.second;
            }
        }
        return weighted_hits;
    };

    std::unordered_map<TaskSecondaryCategory, float> coarse_score_by_class;
    for (std::size_t rule_idx = 0; rule_idx < compiled.secondary_rules.size(); ++rule_idx) {
        const auto &rule = compiled.secondary_rules[rule_idx];
        if (rule.primary != primary || rule.secondary == TaskSecondaryCategory::Unknown) {
            continue;
        }

        const float weighted_hits = weighted_hits_from_cache(rule_idx);
        if (weighted_hits <= 0.0f) {
            continue;
        }

        const float rarity_weight = 1.0f / std::sqrt(static_cast<float>(std::max<std::size_t>(1, rule.keywords.size())));
        const float coarse_piece = std::sqrt(weighted_hits) * rarity_weight;
        coarse_score_by_class[rule.secondary] += coarse_piece;
    }

    std::vector<std::pair<TaskSecondaryCategory, float>> coarse_ranked;
    coarse_ranked.reserve(coarse_score_by_class.size());
    for (const auto &entry : coarse_score_by_class) {
        coarse_ranked.push_back(entry);
    }
    std::sort(coarse_ranked.begin(), coarse_ranked.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    std::vector<TaskSecondaryCategory> stage2_candidates;
    stage2_candidates.reserve(2);
    for (std::size_t i = 0; i < coarse_ranked.size() && i < 2; ++i) {
        stage2_candidates.push_back(coarse_ranked[i].first);
    }

    std::unordered_map<TaskSecondaryCategory, float> refined_score_by_class;
    for (std::size_t rule_idx = 0; rule_idx < compiled.secondary_rules.size(); ++rule_idx) {
        const auto &rule = compiled.secondary_rules[rule_idx];
        if (rule.primary != primary || rule.secondary == TaskSecondaryCategory::Unknown) {
            continue;
        }

        if (!stage2_candidates.empty() &&
            std::find(stage2_candidates.begin(), stage2_candidates.end(), rule.secondary) == stage2_candidates.end()) {
            continue;
        }

        const float weighted_hits = weighted_hits_from_cache(rule_idx);
        if (weighted_hits <= 0.0f) {
            continue;
        }

        const TaskSecondaryCategory cls = CanonicalizeSecondaryCategory(rule.secondary);
        const float rarity_weight = 1.0f / std::sqrt(static_cast<float>(std::max<std::size_t>(1, rule.keywords.size())));
        const float hard_example_bonus = 1.0f + std::min(0.36f, 0.12f * std::max(0.0f, weighted_hits - 1.0f));
        const float class_boost = LongTailClassBoost(primary, cls);

        float coarse_prior = 1.0f;
        const auto coarse_it = coarse_score_by_class.find(rule.secondary);
        if (coarse_it != coarse_score_by_class.end() && !coarse_ranked.empty()) {
            const float top = std::max(1e-6f, coarse_ranked.front().second);
            coarse_prior += 0.15f * std::clamp(coarse_it->second / top, 0.0f, 1.0f);
        }

        const float semantic_score = weighted_hits * rarity_weight * hard_example_bonus * class_boost * coarse_prior;
        const float structured_score = snapshot.secondary_structured_scores[SecondaryCategoryIndex(cls)];
        result.structured_confidence = std::max(result.structured_confidence, std::clamp(structured_score, 0.0f, 1.0f));

        constexpr float kSemanticWeight = 0.72f;
        constexpr float kStructuredWeight = 0.28f;
        constexpr float kOcrStructWeight = 0.22f;
        const float explicit_adjust = ComputeExplicitSignalAdjustment(primary, cls, snapshot.explicit_signals);

        float ocr_struct_bonus = 0.0f;
        switch (cls) {
            case TaskSecondaryCategory::WorkCoding:
            case TaskSecondaryCategory::WorkDebugging:
                ocr_struct_bonus = 0.65f * snapshot.ocr_struct.code_pattern + 0.20f * snapshot.ocr_struct.office_pattern;
                break;
            case TaskSecondaryCategory::WorkReadingDocs:
                ocr_struct_bonus = 0.35f * snapshot.ocr_struct.code_pattern + 0.55f * snapshot.ocr_struct.office_pattern;
                break;
            case TaskSecondaryCategory::WorkMeetingNotes:
                ocr_struct_bonus = 0.50f * snapshot.ocr_struct.office_pattern + 0.40f * snapshot.ocr_struct.chat_pattern;
                break;
            case TaskSecondaryCategory::GameLobby:
            case TaskSecondaryCategory::GameMatch:
            case TaskSecondaryCategory::GameSettlement:
            case TaskSecondaryCategory::GameMenu:
                ocr_struct_bonus = 0.80f * snapshot.ocr_struct.game_ui_pattern;
                break;
            default:
                break;
        }

        const float fused_score = semantic_score * kSemanticWeight +
                                  structured_score * kStructuredWeight +
                                  explicit_adjust +
                                  ocr_struct_bonus * kOcrStructWeight;

        refined_score_by_class[cls] += std::max(0.0f, fused_score);
    }

    std::vector<std::pair<TaskSecondaryCategory, float>> ranked;
    ranked.reserve(refined_score_by_class.size());
    for (const auto &entry : refined_score_by_class) {
        ranked.push_back(entry);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    for (std::size_t i = 0; i < ranked.size() && i < 3; ++i) {
        result.top_candidates.push_back(TaskCategorySecondaryCandidate{
            .category = ranked[i].first,
            .score = ranked[i].second,
        });
    }

    const TaskSecondaryCategory best = ranked.empty() ? TaskSecondaryCategory::Unknown : ranked.front().first;
    const float best_score = ranked.empty() ? 0.0f : ranked.front().second;
    const float second_best_score = ranked.size() > 1 ? ranked[1].second : 0.0f;

    if (best != TaskSecondaryCategory::Unknown) {
        const float abs_conf_threshold = 0.20f;
        const float margin_ratio = (second_best_score > 1e-6f) ? (best_score / second_best_score) : 10.0f;
        const float rel_margin_threshold = 1.12f;
        const bool accepted = best_score >= abs_conf_threshold && margin_ratio >= rel_margin_threshold;
        result.category = accepted ? best : TaskSecondaryCategory::Unknown;
        const float raw_conf = std::clamp(best_score / std::max(1.0f, best_score + second_best_score), 0.0f, 1.0f);
        result.confidence = CalibrateScoreConfidence(raw_conf, 1.20f, 1.08f, -0.04f);
        if (accepted) {
            return result;
        }
    }

    if (primary == TaskPrimaryCategory::Game) {
        result.category = compiled.default_game_secondary != TaskSecondaryCategory::Unknown
                              ? CanonicalizeSecondaryCategory(compiled.default_game_secondary)
                              : TaskSecondaryCategory::GameMatch;
    } else {
        result.category = compiled.default_work_secondary != TaskSecondaryCategory::Unknown
                              ? CanonicalizeSecondaryCategory(compiled.default_work_secondary)
                              : TaskSecondaryCategory::WorkCoding;
    }
    if (result.confidence <= 0.0f) {
        result.confidence = 0.15f;
    }
    return result;
}

PrimaryInferenceResult InferPrimaryCategoryHierarchical(const TaskCategoryFeatureSnapshot &snapshot,
                                                        const TaskCategoryConfig &config) {
    PrimaryInferenceResult result{};
    const float game_semantic =
        static_cast<float>(snapshot.game_primary_hits[0]) * snapshot.source_weights.scene +
        static_cast<float>(snapshot.game_primary_hits[1]) * snapshot.source_weights.ocr +
        static_cast<float>(snapshot.game_primary_hits[2]) * snapshot.source_weights.context;

    const float work_semantic =
        static_cast<float>(snapshot.work_primary_hits[0]) * snapshot.source_weights.scene +
        static_cast<float>(snapshot.work_primary_hits[1]) * snapshot.source_weights.ocr +
        static_cast<float>(snapshot.work_primary_hits[2]) * snapshot.source_weights.context;

    const float game_structured =
        snapshot.game_primary_structured_scores[0] * snapshot.source_weights.scene +
        snapshot.game_primary_structured_scores[1] * snapshot.source_weights.ocr +
        snapshot.game_primary_structured_scores[2] * snapshot.source_weights.context;
    const float work_structured =
        snapshot.work_primary_structured_scores[0] * snapshot.source_weights.scene +
        snapshot.work_primary_structured_scores[1] * snapshot.source_weights.ocr +
        snapshot.work_primary_structured_scores[2] * snapshot.source_weights.context;
    result.game_structured_score = game_structured;
    result.work_structured_score = work_structured;

    const float explicit_game_bias = snapshot.explicit_signals.negation * config.primary.explicit_game_negation_bias +
                                     snapshot.explicit_signals.conditional * config.primary.explicit_game_conditional_bias +
                                     snapshot.explicit_signals.multi_intent * config.primary.explicit_game_multi_intent_bias;
    const float explicit_work_bias = snapshot.explicit_signals.negation * config.primary.explicit_work_negation_bias +
                                     snapshot.explicit_signals.conditional * config.primary.explicit_work_conditional_bias +
                                     snapshot.explicit_signals.multi_intent * config.primary.explicit_work_multi_intent_bias;

    const float game_ocr_struct = config.primary.game_ocr_ui_weight * snapshot.ocr_struct.game_ui_pattern;
    const float work_ocr_struct = config.primary.work_ocr_code_weight * snapshot.ocr_struct.code_pattern +
                                  config.primary.work_ocr_office_weight * snapshot.ocr_struct.office_pattern +
                                  config.primary.work_ocr_chat_weight * snapshot.ocr_struct.chat_pattern;

    result.game_score = game_semantic * config.primary.semantic_weight +
                        game_structured * config.primary.structured_weight +
                        explicit_game_bias * config.primary.explicit_weight +
                        game_ocr_struct * config.primary.ocr_struct_weight;
    result.work_score = work_semantic * config.primary.semantic_weight +
                        work_structured * config.primary.structured_weight +
                        explicit_work_bias * config.primary.explicit_weight +
                        work_ocr_struct * config.primary.ocr_struct_weight;

    result.primary = (result.game_score >= result.work_score) ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
    const float top = std::max(result.game_score, result.work_score);
    const float second = std::min(result.game_score, result.work_score);
    const float base_confidence =
        (top <= 1e-6f) ? 0.0f : std::clamp((top - second) / (std::fabs(top) + 1e-6f), 0.0f, 1.0f);
    result.source_consistency = ComputePrimarySourceConsistency(snapshot, config.primary);
    const float raw_primary_conf = std::clamp(
        base_confidence *
            (config.primary.confidence_consistency_bias +
             config.primary.confidence_consistency_weight * result.source_consistency),
        0.0f,
        1.0f);
    result.confidence = CalibrateScoreConfidence(raw_primary_conf,
                                                 config.primary.confidence_temperature,
                                                 config.primary.confidence_platt_a,
                                                 config.primary.confidence_platt_b);

    const float top_structured = std::max(result.game_structured_score, result.work_structured_score);
    const float second_structured = std::min(result.game_structured_score, result.work_structured_score);
    result.structured_confidence = std::clamp(top_structured - second_structured, 0.0f, 1.0f);
    return result;
}

}  // namespace desktoper2D::task_category_internal
