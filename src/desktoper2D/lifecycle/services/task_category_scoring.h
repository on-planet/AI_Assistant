#pragma once

#include <vector>

#include "desktoper2D/lifecycle/services/task_category_features.h"

namespace desktoper2D::task_category_internal {

struct SecondaryInferenceResult {
    TaskSecondaryCategory category = TaskSecondaryCategory::Unknown;
    float confidence = 0.0f;
    float structured_confidence = 0.0f;
    std::vector<TaskCategorySecondaryCandidate> top_candidates;
};

struct PrimaryInferenceResult {
    TaskPrimaryCategory primary = TaskPrimaryCategory::Work;
    float game_score = 0.0f;
    float work_score = 0.0f;
    float game_structured_score = 0.0f;
    float work_structured_score = 0.0f;
    float confidence = 0.0f;
    float structured_confidence = 0.0f;
    float source_consistency = 0.0f;
};

SecondaryInferenceResult InferSecondaryCategory(const TaskCategoryFeatureSnapshot &snapshot,
                                                TaskPrimaryCategory primary,
                                                const TaskCategoryCompiledConfig &compiled,
                                                const TaskCategoryConfig &config);

PrimaryInferenceResult InferPrimaryCategoryHierarchical(const TaskCategoryFeatureSnapshot &snapshot,
                                                        const TaskCategoryConfig &config);

}  // namespace desktoper2D::task_category_internal
