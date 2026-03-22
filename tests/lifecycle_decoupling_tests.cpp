#include "desktoper2D/lifecycle/asr/asr_session_service.h"
#include "desktoper2D/lifecycle/services/decision_service.h"

#include <iostream>
#include <string>

namespace {

using desktoper2D::AsrRecognitionResult;
using desktoper2D::AsrSessionState;
using desktoper2D::BuildTaskDecisionInputSignatureFromComponents;
using desktoper2D::ComputeTaskDecision;
using desktoper2D::HashTaskDecisionAsrSessionText;
using desktoper2D::HashTaskDecisionOcrResult;
using desktoper2D::HashTaskDecisionSceneResult;
using desktoper2D::HashTaskDecisionSystemContext;
using desktoper2D::OcrResult;
using desktoper2D::SceneClassificationResult;
using desktoper2D::SystemContextSnapshot;
using desktoper2D::TaskCategoryConfig;
using desktoper2D::TaskCategoryDecisionMemory;
using desktoper2D::TaskCategoryInferenceResult;
using desktoper2D::TaskCategoryInferenceState;
using desktoper2D::TaskPrimaryCategory;
using desktoper2D::TaskCategoryScheduleState;
using desktoper2D::TaskSecondaryCategory;
using desktoper2D::TickTaskDecision;
using desktoper2D::UpdateAsrSessionState;

bool Check(bool ok, const char *msg) {
    if (!ok) {
        std::cerr << "[FAIL] " << msg << "\n";
        return false;
    }
    return true;
}

std::uint64_t BuildInputSignature(const SystemContextSnapshot &ctx,
                                  const OcrResult &ocr,
                                  const SceneClassificationResult &scene,
                                  const std::string &asr_session_text) {
    return BuildTaskDecisionInputSignatureFromComponents(
        HashTaskDecisionSystemContext(ctx),
        HashTaskDecisionOcrResult(ocr),
        HashTaskDecisionSceneResult(scene),
        HashTaskDecisionAsrSessionText(asr_session_text));
}

bool TestAsrSessionStateUpdate() {
    AsrSessionState state{};
    state.max_utterances = 2;

    AsrRecognitionResult r1{};
    r1.ok = true;
    r1.is_final = true;
    r1.text = "hello";
    UpdateAsrSessionState(r1, state);

    AsrRecognitionResult r2{};
    r2.ok = true;
    r2.is_final = true;
    r2.text = "world";
    UpdateAsrSessionState(r2, state);

    AsrRecognitionResult r3{};
    r3.ok = true;
    r3.is_final = true;
    r3.text = "again";
    UpdateAsrSessionState(r3, state);

    if (!Check(state.utterances.size() == 2, "asr session should keep max_utterances")) return false;
    if (!Check(state.utterances.front() == "world", "asr session should pop front when full")) return false;
    if (!Check(state.session_text == "world\nagain", "asr session_text should join utterances")) return false;

    AsrRecognitionResult partial{};
    partial.ok = true;
    partial.is_final = false;
    partial.text = "partial";
    UpdateAsrSessionState(partial, state);
    if (!Check(state.session_text == "world\nagain", "non-final asr result should not mutate session")) return false;

    return true;
}

bool TestDecisionServiceSmoke() {
    SystemContextSnapshot ctx{};
    ctx.process_name = "code.exe";
    ctx.window_title = "debug stacktrace";

    OcrResult ocr{};
    ocr.summary = "exception stacktrace assert";

    SceneClassificationResult scene{};
    scene.label = "ide";
    scene.score = 0.8f;

    TaskCategoryConfig config{};
    config.decision.primary_consistency_threshold = 0.0f;
    config.decision.primary_reject_threshold = 0.0f;
    config.decision.weak_source_primary_reject_threshold = 0.0f;
    config.decision.secondary_reject_threshold = 0.05f;
    config.decision.weak_source_secondary_reject_threshold = 0.05f;

    TaskCategoryInferenceResult out{};
    TaskCategoryInferenceState task_state{};
    std::string asr_session_text = "desktoper2d help me debug this crash";

    ComputeTaskDecision(ctx, ocr, scene, config, task_state, &asr_session_text, out, nullptr);

    if (!Check(out.primary != TaskPrimaryCategory::Unknown, "decision primary should be inferred")) return false;
    if (!Check(out.secondary != TaskSecondaryCategory::Unknown, "decision secondary should be inferred")) return false;
    if (!Check(out.primary_confidence >= 0.0f && out.primary_confidence <= 1.0f, "primary confidence should be in [0,1]")) return false;

    TaskCategoryConfig strict_config = config;
    strict_config.decision.primary_consistency_threshold = 1.0f;
    strict_config.decision.primary_reject_threshold = 1.0f;
    strict_config.decision.weak_source_primary_reject_threshold = 1.0f;

    TaskCategoryInferenceResult strict_out{};
    TaskCategoryInferenceState strict_state{};
    ComputeTaskDecision(ctx, ocr, scene, strict_config, strict_state, &asr_session_text, strict_out, nullptr);

    if (!Check(strict_out.primary == TaskPrimaryCategory::Unknown, "decision config should be able to reject primary")) return false;
    if (!Check(strict_out.secondary == TaskSecondaryCategory::Unknown, "decision reject should clear secondary")) return false;

    return true;
}

bool TestRejectStateMemoryPersistence() {
    SystemContextSnapshot ctx{};
    ctx.process_name = "code.exe";
    ctx.window_title = "debug stacktrace";

    OcrResult ocr{};
    ocr.summary = "exception stacktrace assert";

    SceneClassificationResult scene{};
    scene.label = "ide";
    scene.score = 0.8f;

    TaskCategoryConfig config{};
    config.decision.primary_consistency_threshold = 1.0f;
    config.decision.primary_reject_threshold = 1.0f;
    config.decision.weak_source_primary_reject_threshold = 1.0f;

    std::string asr_session_text = "desktoper2d help me debug this crash";
    TaskCategoryInferenceState state{};
    TaskCategoryInferenceResult out1{};
    ComputeTaskDecision(ctx, ocr, scene, config, state, &asr_session_text, out1, nullptr);

    if (!Check(out1.primary == TaskPrimaryCategory::Unknown, "first reject run should return unknown primary")) return false;
    if (!Check(state.decision.last_state == TaskCategoryDecisionMemory::StateTag::RejectPrimary,
               "first reject run should persist RejectPrimary state")) return false;
    if (!Check(state.decision.hold_frames == 1, "first reject run should initialize hold_frames to 1")) return false;

    TaskCategoryInferenceResult out2{};
    ComputeTaskDecision(ctx, ocr, scene, config, state, &asr_session_text, out2, nullptr);

    if (!Check(out2.primary == TaskPrimaryCategory::Unknown, "second reject run should still return unknown primary")) return false;
    if (!Check(state.decision.last_state == TaskCategoryDecisionMemory::StateTag::RejectPrimary,
               "second reject run should keep RejectPrimary state")) return false;
    if (!Check(state.decision.hold_frames == 2, "second reject run should increment hold_frames")) return false;

    return true;
}

bool TestWeakSourcePrimaryRejectThreshold() {
    SystemContextSnapshot ctx{};
    OcrResult ocr{};
    SceneClassificationResult scene{};

    TaskCategoryConfig weak_reject_config{};
    weak_reject_config.decision.primary_consistency_threshold = 0.0f;
    weak_reject_config.decision.primary_reject_threshold = 0.0f;
    weak_reject_config.decision.weak_source_primary_reject_threshold = 0.99f;
    weak_reject_config.decision.min_reliable_source_weight = 0.42f;

    TaskCategoryInferenceState weak_state{};
    TaskCategoryInferenceResult weak_out{};
    ComputeTaskDecision(ctx, ocr, scene, weak_reject_config, weak_state, nullptr, weak_out, nullptr);
    if (!Check(weak_out.primary == TaskPrimaryCategory::Unknown,
               "weak source config should be able to reject primary")) return false;

    TaskCategoryConfig relaxed_config = weak_reject_config;
    relaxed_config.decision.min_reliable_source_weight = 0.20f;

    TaskCategoryInferenceState relaxed_state{};
    TaskCategoryInferenceResult relaxed_out{};
    ComputeTaskDecision(ctx, ocr, scene, relaxed_config, relaxed_state, nullptr, relaxed_out, nullptr);
    if (!Check(relaxed_out.primary != TaskPrimaryCategory::Unknown,
               "lower min_reliable_source_weight should relax weak-source rejection")) return false;

    return true;
}

bool TestTickTaskDecisionThrottle() {
    SystemContextSnapshot ctx{};
    ctx.process_name = "code.exe";
    ctx.window_title = "debug stacktrace";

    OcrResult ocr{};
    ocr.summary = "exception stacktrace assert";

    SceneClassificationResult scene{};
    scene.label = "ide";
    scene.score = 0.8f;

    TaskCategoryConfig config{};
    config.decision.primary_consistency_threshold = 0.0f;
    config.decision.primary_reject_threshold = 0.0f;
    config.decision.weak_source_primary_reject_threshold = 0.0f;
    config.decision.secondary_reject_threshold = 0.05f;
    config.decision.weak_source_secondary_reject_threshold = 0.05f;

    TaskCategoryScheduleState schedule{};
    schedule.min_interval_sec = 0.5f;
    TaskCategoryInferenceState state{};
    TaskCategoryInferenceResult result{};
    std::string asr_session_text = "desktoper2d help me debug this crash";
    std::uint64_t input_signature = BuildInputSignature(ctx, ocr, scene, asr_session_text);

    const bool first_computed = TickTaskDecision(ctx,
                                                 ocr,
                                                 scene,
                                                 input_signature,
                                                 0.1f,
                                                 config,
                                                 schedule,
                                                 state,
                                                 &asr_session_text,
                                                 result,
                                                 nullptr);
    if (!Check(first_computed, "first throttled tick should compute")) return false;
    if (!Check(schedule.compute_count == 1, "first throttled tick should increment compute_count")) return false;

    const bool second_computed = TickTaskDecision(ctx,
                                                  ocr,
                                                  scene,
                                                  input_signature,
                                                  0.1f,
                                                  config,
                                                  schedule,
                                                  state,
                                                  &asr_session_text,
                                                  result,
                                                  nullptr);
    if (!Check(!second_computed, "unchanged input before interval should skip compute")) return false;
    if (!Check(schedule.skipped_count == 1, "skipped tick should increment skipped_count")) return false;

    scene.label = "browser";
    input_signature = BuildInputSignature(ctx, ocr, scene, asr_session_text);
    const bool changed_input_computed = TickTaskDecision(ctx,
                                                         ocr,
                                                         scene,
                                                         input_signature,
                                                         0.1f,
                                                         config,
                                                         schedule,
                                                         state,
                                                         &asr_session_text,
                                                         result,
                                                         nullptr);
    if (!Check(changed_input_computed, "changed input should bypass throttle")) return false;

    return true;
}

bool TestSecondaryCategoryReachability() {
    {
        SystemContextSnapshot ctx{};
        ctx.process_name = "steam.exe";
        ctx.window_title = "game menu settings inventory";

        OcrResult ocr{};
        ocr.summary = "menu settings inventory loadout store";

        SceneClassificationResult scene{};
        scene.label = "game menu";
        scene.score = 0.95f;

        TaskCategoryConfig config{};
        config.game_primary_keywords = {"game", "menu", "settings", "inventory", "steam"};
        config.work_primary_keywords = {"code", "debug"};
        config.secondary_rules = {
            {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameMenu, {"menu", "settings", "inventory", "loadout", "store"}},
            {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameMatch, {"crosshair", "ammo", "kill"}},
            {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkDebugging, {"debug", "stacktrace"}},
        };
        config.decision.primary_consistency_threshold = 0.0f;
        config.decision.primary_reject_threshold = 0.0f;
        config.decision.weak_source_primary_reject_threshold = 0.0f;
        config.decision.secondary_reject_threshold = 0.0f;
        config.decision.weak_source_secondary_reject_threshold = 0.0f;

        TaskCategoryInferenceState state{};
        TaskCategoryInferenceResult out{};
        ComputeTaskDecision(ctx, ocr, scene, config, state, nullptr, out, nullptr);
        if (!Check(out.primary == TaskPrimaryCategory::Game, "game-menu case should infer game primary")) return false;
        if (!Check(out.secondary == TaskSecondaryCategory::GameMenu,
                   "game-menu case should keep GameMenu secondary reachable")) return false;
    }

    {
        SystemContextSnapshot ctx{};
        ctx.process_name = "code.exe";
        ctx.window_title = "debug stacktrace";

        OcrResult ocr{};
        ocr.summary = "debug breakpoint exception stacktrace crash";

        SceneClassificationResult scene{};
        scene.label = "ide";
        scene.score = 0.95f;

        TaskCategoryConfig config{};
        config.game_primary_keywords = {"game", "match"};
        config.work_primary_keywords = {"code", "debug", "stacktrace", "exception"};
        config.secondary_rules = {
            {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkDebugging, {"debug", "breakpoint", "exception", "stacktrace", "crash"}},
            {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkReadingDocs, {"readme", "docs", "wiki"}},
            {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameMatch, {"kill", "crosshair"}},
        };
        config.decision.primary_consistency_threshold = 0.0f;
        config.decision.primary_reject_threshold = 0.0f;
        config.decision.weak_source_primary_reject_threshold = 0.0f;
        config.decision.secondary_reject_threshold = 0.0f;
        config.decision.weak_source_secondary_reject_threshold = 0.0f;

        TaskCategoryInferenceState state{};
        TaskCategoryInferenceResult out{};
        ComputeTaskDecision(ctx, ocr, scene, config, state, nullptr, out, nullptr);
        if (!Check(out.primary == TaskPrimaryCategory::Work, "work-debug case should infer work primary")) return false;
        if (!Check(out.secondary == TaskSecondaryCategory::WorkDebugging,
                   "work-debug case should keep WorkDebugging secondary reachable")) return false;
    }

    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok = TestAsrSessionStateUpdate() && ok;
    ok = TestDecisionServiceSmoke() && ok;
    ok = TestRejectStateMemoryPersistence() && ok;
    ok = TestWeakSourcePrimaryRejectThreshold() && ok;
    ok = TestTickTaskDecisionThrottle() && ok;
    ok = TestSecondaryCategoryReachability() && ok;

    if (!ok) {
        std::cerr << "lifecycle_decoupling_tests failed\n";
        return 1;
    }

    std::cout << "lifecycle_decoupling_tests passed\n";
    return 0;
}
