#include "k2d/lifecycle/systems/app_systems.h"

#include <algorithm>
#include <ctime>
#include <string>

#include <SDL3/SDL_log.h>

#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/asr/asr_provider.h"
#include "k2d/lifecycle/asr/vad_segmenter.h"

namespace k2d {

void TickAppSystems(AppRuntime &runtime, float dt) {
    runtime.perception_state.scene_classifier_enabled = runtime.feature_scene_classifier_enabled;
    runtime.perception_state.ocr_enabled = runtime.feature_ocr_enabled;
    runtime.perception_state.camera_facemesh_enabled = runtime.feature_face_emotion_enabled;
    runtime.perception_state.system_context_enabled =
        runtime.feature_scene_classifier_enabled || runtime.feature_ocr_enabled || runtime.feature_face_emotion_enabled;
    runtime.perception_state.enabled = runtime.perception_state.system_context_enabled;

    // 迁移批次 #1：Reminder + Perception
    if (runtime.reminder_ready) {
        runtime.reminder_poll_accum_sec += std::max(0.0f, dt);
        if (runtime.reminder_poll_accum_sec >= 1.0f) {
            runtime.reminder_poll_accum_sec = 0.0f;
            const std::int64_t now_sec = static_cast<std::int64_t>(std::time(nullptr));

            std::string due_err;
            auto due_items = runtime.reminder_service.PollDueAndMarkNotified(now_sec, 8, &due_err);
            if (!due_err.empty()) {
                runtime.reminder_last_error = due_err;
            }
            for (const auto &item : due_items) {
                SDL_Log("[Reminder] due id=%lld title=%s", static_cast<long long>(item.id), item.title.c_str());
            }

            std::string list_err;
            runtime.reminder_upcoming = runtime.reminder_service.ListUpcoming(now_sec, 8, &list_err);
            if (!list_err.empty()) {
                runtime.reminder_last_error = list_err;
            }
        }
    }

    runtime.perception_pipeline.Tick(dt, runtime.perception_state);

    if (runtime.feature_asr_enabled && runtime.asr_ready && runtime.asr_provider) {
        runtime.asr_poll_accum_sec += std::max(0.0f, dt);
        if (runtime.asr_poll_accum_sec >= 0.02f) {
            runtime.asr_poll_accum_sec = 0.0f;

            // TODO: 接入真实麦克风 PCM 数据。这里先用静音帧做行为占位。
            runtime.asr_audio_buffer.assign(static_cast<std::size_t>(runtime.asr_frame_samples), 0.0f);

            VadFrame frame{};
            frame.sample_rate_hz = 16000;
            frame.samples = runtime.asr_audio_buffer;

            VadSegment seg{};
            if (runtime.asr_vad.Accept(frame, seg)) {
                AsrAudioChunk chunk{};
                chunk.sample_rate_hz = seg.sample_rate_hz;
                chunk.is_final = seg.is_final;
                chunk.samples = seg.samples;

                AsrRecognitionOptions options{};
                options.language = "zh";

                AsrRecognitionResult result{};
                std::string asr_err;
                const bool ok = runtime.asr_provider->Recognize(chunk, options, result, &asr_err);

                runtime.asr_total_segments += 1;
                const double audio_sec = static_cast<double>(chunk.samples.size()) /
                                         static_cast<double>(std::max(1, chunk.sample_rate_hz));
                const double infer_sec = static_cast<double>(std::max<std::int64_t>(0, result.latency_ms)) / 1000.0;
                runtime.asr_audio_total_sec += audio_sec;
                runtime.asr_infer_total_sec += infer_sec;
                runtime.asr_rtf = runtime.asr_audio_total_sec > 1e-9
                                      ? (runtime.asr_infer_total_sec / runtime.asr_audio_total_sec)
                                      : 0.0;

                if (result.timeout_detected) runtime.asr_timeout_segments += 1;
                if (result.cloud_attempted) runtime.asr_cloud_attempts += 1;
                if (result.cloud_succeeded) runtime.asr_cloud_success += 1;
                if (result.fallback_to_offline) runtime.asr_cloud_fallbacks += 1;

                runtime.asr_timeout_rate = runtime.asr_total_segments > 0
                                               ? static_cast<double>(runtime.asr_timeout_segments) /
                                                     static_cast<double>(runtime.asr_total_segments)
                                               : 0.0;
                runtime.asr_cloud_call_ratio = runtime.asr_total_segments > 0
                                                   ? static_cast<double>(runtime.asr_cloud_attempts) /
                                                         static_cast<double>(runtime.asr_total_segments)
                                                   : 0.0;
                runtime.asr_cloud_success_ratio = runtime.asr_cloud_attempts > 0
                                                      ? static_cast<double>(runtime.asr_cloud_success) /
                                                            static_cast<double>(runtime.asr_cloud_attempts)
                                                      : 0.0;
                runtime.asr_wer_proxy = std::clamp(1.0 - static_cast<double>(result.confidence), 0.0, 1.0);
                runtime.asr_last_switch_reason = result.switch_reason;

                if (ok) {
                    runtime.asr_last_result = std::move(result);
                    runtime.asr_last_error.clear();
                } else {
                    runtime.asr_last_error = asr_err;
                    SDL_Log("ASR recognize failed: %s", asr_err.c_str());
                }
            }
        }
    }
}

}  // namespace k2d
