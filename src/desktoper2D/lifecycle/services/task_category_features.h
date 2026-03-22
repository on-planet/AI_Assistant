#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "desktoper2D/lifecycle/perception_pipeline.h"
#include "desktoper2D/lifecycle/state/task_category_types.h"

namespace desktoper2D::task_category_internal {

inline constexpr std::size_t kTaskSourceCount = 3;
inline constexpr std::size_t kTaskSecondaryCategoryCount =
    static_cast<std::size_t>(TaskSecondaryCategory::GameMenu) + 1;

struct DynamicSourceWeights {
    float scene = 0.33f;
    float ocr = 0.33f;
    float context = 0.34f;
};

struct StructuredFeatureLexicon {
    std::vector<std::string> action_terms;
    std::vector<std::string> time_terms;
    std::vector<std::string> object_terms;
};

struct ExplicitSentenceSignals {
    float negation = 0.0f;
    float conditional = 0.0f;
    float multi_intent = 0.0f;
};

struct OcrStructuredSignals {
    float code_pattern = 0.0f;
    float office_pattern = 0.0f;
    float game_ui_pattern = 0.0f;
    float chat_pattern = 0.0f;
};

struct TaskCategoryFeatureSnapshot {
    std::string scene_text;
    std::string ocr_text;
    std::string ctx_text;
    DynamicSourceWeights source_weights{};
    std::array<std::pair<const std::string *, float>, kTaskSourceCount> source_texts{};
    ExplicitSentenceSignals explicit_signals{};
    OcrStructuredSignals ocr_struct{};
    std::array<int, kTaskSourceCount> game_primary_hits{};
    std::array<int, kTaskSourceCount> work_primary_hits{};
    std::array<float, kTaskSourceCount> game_primary_structured_scores{};
    std::array<float, kTaskSourceCount> work_primary_structured_scores{};
    std::vector<std::array<int, kTaskSourceCount>> secondary_rule_hits;
    std::array<float, kTaskSecondaryCategoryCount> secondary_structured_scores{};
};

std::string ToLower(std::string s);
std::size_t SecondaryCategoryIndex(TaskSecondaryCategory category);
TaskPrimaryCategory PrimaryFromSecondary(TaskSecondaryCategory category);
TaskCategoryCompiledConfig BuildCompiledConfig(const TaskCategoryConfig &config);
const TaskCategoryCompiledConfig &EnsureCompiledConfig(const TaskCategoryConfig &config,
                                                      TaskCategoryInferenceState &inout_state);
int CountKeywordHits(const std::string &text, const std::vector<std::string> &keywords);
float CalibrateScoreConfidence(float raw_confidence,
                               float temperature,
                               float platt_a,
                               float platt_b);
TaskCategoryFeatureSnapshot BuildTaskCategoryFeatureSnapshot(const SystemContextSnapshot &ctx,
                                                            const OcrResult &ocr,
                                                            const SceneClassificationResult &scene,
                                                            const TaskCategoryConfig &config,
                                                            const TaskCategoryCompiledConfig &compiled,
                                                            const std::string *asr_session_text);

}  // namespace desktoper2D::task_category_internal
