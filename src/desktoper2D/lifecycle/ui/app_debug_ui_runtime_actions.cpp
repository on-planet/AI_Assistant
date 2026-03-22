#include "desktoper2D/lifecycle/ui/app_debug_ui_runtime_actions.h"

#include <algorithm>
#include <filesystem>

#include <SDL3/SDL_iostream.h>

#include "app_debug_ui_presenter.h"
#include "desktoper2D/lifecycle/services/decision_service.h"

namespace desktoper2D {

void ResetPerceptionRuntimeState(PerceptionPipelineState &state) {
    state.screen_capture_poll_accum_sec = 0.0f;
    state.screen_capture_last_error.clear();
    state.screen_capture_success_count = 0;
    state.screen_capture_fail_count = 0;
    ClearRuntimeError(state.capture_error_info);

    state.scene_classifier_last_error.clear();
    state.scene_result = SceneClassificationResult{};
    state.scene_total_runs = 0;
    state.scene_total_latency_ms = 0;
    state.scene_avg_latency_ms = 0.0f;
    ClearRuntimeError(state.scene_error_info);

    state.ocr_last_error.clear();
    state.ocr_result = OcrResult{};
    state.ocr_last_stable_result = OcrResult{};
    state.ocr_skipped_due_timeout = false;
    state.ocr_total_runs = 0;
    state.ocr_total_latency_ms = 0;
    state.ocr_avg_latency_ms = 0.0f;
    state.ocr_preprocess_det_avg_ms = 0.0f;
    state.ocr_infer_det_avg_ms = 0.0f;
    state.ocr_preprocess_rec_avg_ms = 0.0f;
    state.ocr_infer_rec_avg_ms = 0.0f;
    state.ocr_total_raw_lines = 0;
    state.ocr_total_kept_lines = 0;
    state.ocr_total_dropped_low_conf_lines = 0;
    state.ocr_discard_rate = 0.0f;
    state.ocr_conf_low_count = 0;
    state.ocr_conf_mid_count = 0;
    state.ocr_conf_high_count = 0;
    state.ocr_summary_candidate.clear();
    state.ocr_summary_stable.clear();
    state.ocr_summary_consistent_count = 0;
    ClearRuntimeError(state.ocr_error_info);

    state.system_context_snapshot = SystemContextSnapshot{};
    state.system_context_last_error.clear();
    ClearRuntimeError(state.system_context_error_info);

    state.camera_facemesh_last_error.clear();
    state.face_emotion_result = FaceEmotionResult{};
    state.face_total_runs = 0;
    state.face_total_latency_ms = 0;
    state.face_avg_latency_ms = 0.0f;
    ClearRuntimeError(state.facemesh_error_info);

    state.blackboard = PerceptionBlackboard{};
    ResetTaskDecisionInputCache(state);
}

void ResetAllRuntimeErrorCounters(AppRuntime &runtime) {
    auto reset_err = [](RuntimeErrorInfo &err) {
        ClearRuntimeError(err);
        err.detail.clear();
        err.count = 0;
        err.degraded_count = 0;
    };

    reset_err(runtime.perception_state.capture_error_info);
    reset_err(runtime.perception_state.scene_error_info);
    reset_err(runtime.perception_state.ocr_error_info);
    reset_err(runtime.perception_state.system_context_error_info);
    reset_err(runtime.perception_state.facemesh_error_info);
    reset_err(runtime.plugin.error_info);
    reset_err(runtime.asr_error_info);
    reset_err(runtime.chat_error_info);
    reset_err(runtime.reminder_error_info);
}

bool ExportRuntimeSnapshotJson(const AppRuntime &runtime, const char *path, std::string *out_error) {
    const JsonValue snapshot = BuildRuntimeSnapshotJson(runtime);
    const std::string text = StringifyJson(snapshot, 2);

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        if (out_error) {
            *out_error = std::string("open snapshot file failed: ") + SDL_GetError();
        }
        return false;
    }

    const size_t n = text.size();
    const size_t w = SDL_WriteIO(io, text.data(), n);
    SDL_CloseIO(io);

    if (w != n) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (out_error) {
            *out_error = "write snapshot file failed";
        }
        return false;
    }

    if (out_error) {
        out_error->clear();
    }
    return true;
}

void TriggerSingleStepSampling(AppRuntime &runtime) {
    runtime.perception_state.screen_capture_poll_accum_sec =
        std::max(0.1f, runtime.perception_state.screen_capture_poll_interval_sec);
    runtime.reminder_poll_accum_sec = 1.0f;
    runtime.asr_poll_accum_sec = 0.02f;
    runtime.observability.log_accum_sec =
        std::max(0.2f, runtime.observability.log_interval_sec);
}

}  // namespace desktoper2D
