#include "k2d/lifecycle/systems/app_systems.h"

#include <algorithm>
#include <ctime>
#include <string>

#include "k2d/core/async_logger.h"
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
                UpdateRuntimeError(runtime.reminder_error_info,
                                   RuntimeErrorDomain::Reminder,
                                   RuntimeErrorCode::InternalError,
                                   due_err);
            }
            for (const auto &item : due_items) {
                LogInfo("[obs] domain=reminder code=OK detail=due id=%lld title=%s",
                        static_cast<long long>(item.id),
                        item.title.c_str());
            }

            std::string list_err;
            runtime.reminder_upcoming = runtime.reminder_service.ListUpcoming(now_sec, 8, &list_err);
            if (!list_err.empty()) {
                runtime.reminder_last_error = list_err;
                UpdateRuntimeError(runtime.reminder_error_info,
                                   RuntimeErrorDomain::Reminder,
                                   RuntimeErrorCode::InternalError,
                                   list_err);
            } else if (due_err.empty()) {
                ClearRuntimeError(runtime.reminder_error_info);
            }
        }
    }

    runtime.perception_pipeline.Tick(dt, runtime.perception_state);

    if (runtime.feature_asr_enabled && runtime.asr_ready && runtime.asr_provider) {
        runtime.asr_poll_accum_sec += std::max(0.0f, dt);
        if (runtime.asr_poll_accum_sec >= 0.02f) {
            runtime.asr_poll_accum_sec = 0.0f;

            // 麦克风队列取 20ms 帧；不足时补静音。
            runtime.asr_audio_buffer.clear();
            runtime.asr_audio_buffer.reserve(static_cast<std::size_t>(runtime.asr_frame_samples));
            {
                std::lock_guard<std::mutex> lk(runtime.mic_mutex);
                while (!runtime.mic_pcm_queue.empty() &&
                       runtime.asr_audio_buffer.size() < static_cast<std::size_t>(runtime.asr_frame_samples)) {
                    runtime.asr_audio_buffer.push_back(runtime.mic_pcm_queue.front());
                    runtime.mic_pcm_queue.pop_front();
                }
            }
            while (runtime.asr_audio_buffer.size() < static_cast<std::size_t>(runtime.asr_frame_samples)) {
                runtime.asr_audio_buffer.push_back(0.0f);
            }

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
                    RuntimeErrorCode asr_code = RuntimeErrorCode::InferenceFailed;
                    if (result.timeout_detected) {
                        asr_code = RuntimeErrorCode::TimeoutDegraded;
                    }
                    if (asr_code == RuntimeErrorCode::TimeoutDegraded) {
                        UpdateRuntimeDegrade(runtime.asr_error_info,
                                             RuntimeErrorDomain::Asr,
                                             asr_code,
                                             asr_err);
                    } else {
                        UpdateRuntimeError(runtime.asr_error_info,
                                           RuntimeErrorDomain::Asr,
                                           asr_code,
                                           asr_err);
                    }
                    LogError("[obs] domain=%s code=%s detail=%s",
                             RuntimeErrorDomainName(runtime.asr_error_info.domain),
                             RuntimeErrorCodeName(runtime.asr_error_info.code),
                             runtime.asr_error_info.detail.c_str());
                }
            }
        }
    }

    if (runtime.plugin_ready && runtime.inference_adapter) {
        const PluginWorkerStats stats = runtime.inference_adapter->GetStats();
        runtime.plugin_total_update_count = stats.total_update_count;
        runtime.plugin_timeout_count = stats.timeout_count;
        runtime.plugin_exception_count = stats.exception_count;
        runtime.plugin_internal_error_count = stats.internal_error_count;
        runtime.plugin_disable_count = stats.disable_count;
        runtime.plugin_recover_count = stats.recover_count;
        runtime.plugin_timeout_rate = stats.total_update_count > 0
                                          ? static_cast<double>(stats.timeout_count) / static_cast<double>(stats.total_update_count)
                                          : 0.0;
        runtime.plugin_current_update_hz = stats.current_update_hz;
        runtime.plugin_auto_disabled = stats.auto_disabled;
        runtime.plugin_last_error = stats.last_error;

        if (stats.auto_disabled) {
            UpdateRuntimeError(runtime.plugin_error_info,
                               RuntimeErrorDomain::PluginWorker,
                               RuntimeErrorCode::AutoDisabled,
                               stats.last_error.empty() ? std::string("plugin auto disabled") : stats.last_error);
        } else if (stats.internal_error_count > 0 || stats.exception_count > 0 || stats.timeout_count > 0) {
            if (stats.internal_error_count == 0 && stats.exception_count == 0 && stats.timeout_count > 0) {
                UpdateRuntimeDegrade(runtime.plugin_error_info,
                                     RuntimeErrorDomain::PluginWorker,
                                     RuntimeErrorCode::TimeoutDegraded,
                                     stats.last_error.empty() ? std::string("plugin timeout degraded") : stats.last_error);
            } else {
                UpdateRuntimeError(runtime.plugin_error_info,
                                   RuntimeErrorDomain::PluginWorker,
                                   RuntimeErrorCode::InternalError,
                                   stats.last_error);
            }
        } else {
            ClearRuntimeError(runtime.plugin_error_info);
        }
    } else {
        runtime.plugin_total_update_count = 0;
        runtime.plugin_timeout_count = 0;
        runtime.plugin_exception_count = 0;
        runtime.plugin_internal_error_count = 0;
        runtime.plugin_disable_count = 0;
        runtime.plugin_recover_count = 0;
        runtime.plugin_timeout_rate = 0.0;
        runtime.plugin_current_update_hz = 0;
        runtime.plugin_auto_disabled = false;
    }

    runtime.runtime_observability_log_accum_sec += std::max(0.0f, dt);
    if (runtime.runtime_observability_log_enabled &&
        runtime.runtime_observability_log_accum_sec >= std::max(0.2f, runtime.runtime_observability_log_interval_sec)) {
        runtime.runtime_observability_log_accum_sec = 0.0f;

        LogInfo("[obs][metrics] fps=%.2f frame_ms=%.2f capture_ok=%lld capture_fail=%lld scene_ms=%.1f ocr_ms=%.1f face_ms=%.1f asr_rtf=%.3f plugin_hz=%d plugin_auto_disabled=%s",
                runtime.debug_fps,
                runtime.debug_frame_ms,
                static_cast<long long>(runtime.perception_state.screen_capture_success_count),
                static_cast<long long>(runtime.perception_state.screen_capture_fail_count),
                runtime.perception_state.scene_avg_latency_ms,
                runtime.perception_state.ocr_avg_latency_ms,
                runtime.perception_state.face_avg_latency_ms,
                runtime.asr_rtf,
                runtime.plugin_current_update_hz,
                runtime.plugin_auto_disabled ? "true" : "false");

        const auto log_err = [](const RuntimeErrorInfo &err) {
            if (err.code != RuntimeErrorCode::Ok) {
                LogError("[obs][error] domain=%s code=%s count=%lld degraded=%lld detail=%s",
                         RuntimeErrorDomainName(err.domain),
                         RuntimeErrorCodeName(err.code),
                         static_cast<long long>(err.count),
                         static_cast<long long>(err.degraded_count),
                         err.detail.c_str());
            }
        };

        log_err(runtime.perception_state.capture_error_info);
        log_err(runtime.perception_state.scene_error_info);
        log_err(runtime.perception_state.ocr_error_info);
        log_err(runtime.perception_state.system_context_error_info);
        log_err(runtime.perception_state.facemesh_error_info);
        log_err(runtime.plugin_error_info);
        log_err(runtime.asr_error_info);
        log_err(runtime.chat_error_info);
        log_err(runtime.reminder_error_info);
    }
}

}  // namespace k2d
