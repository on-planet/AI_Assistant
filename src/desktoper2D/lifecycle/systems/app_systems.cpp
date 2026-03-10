#include "desktoper2D/lifecycle/systems/app_systems.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>

#include "desktoper2D/core/async_logger.h"
#include "desktoper2D/lifecycle/asr/asr_provider.h"
#include "desktoper2D/lifecycle/asr/asr_session_service.h"
#include "desktoper2D/lifecycle/asr/vad_segmenter.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

namespace {

double ComputeP95(std::vector<double> samples) {
    if (samples.empty()) {
        return 0.0;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t idx = static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(samples.size() - 1)));
    return samples[std::min(idx, samples.size() - 1)];
}

void PushLatency(std::vector<double> &window,
                 std::size_t cap,
                 std::size_t &head,
                 std::size_t &size,
                 double ms) {
    if (cap == 0) {
        window.clear();
        head = 0;
        size = 0;
        return;
    }
    if (window.size() != cap) {
        window.assign(cap, 0.0);
        head = 0;
        size = 0;
    }
    const std::size_t idx = (head + size) % cap;
    window[idx] = std::max(0.0, ms);
    if (size < cap) {
        ++size;
    } else {
        head = (head + 1) % cap;
    }
}

std::vector<double> SnapshotRing(const std::vector<double> &ring, std::size_t head, std::size_t size) {
    std::vector<double> out;
    out.reserve(size);
    if (ring.empty() || size == 0) {
        return out;
    }
    const std::size_t cap = ring.size();
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(ring[(head + i) % cap]);
    }
    return out;
}

void PushMetricsSample(AppRuntime &runtime) {
    RuntimeMetricsSample sample{};
    sample.ts_ms = ObsNowTsMs();
    sample.seq = ++runtime.runtime_metrics_seq;

    const double cap_ok = static_cast<double>(runtime.perception_state.screen_capture_success_count);
    const double cap_fail = static_cast<double>(runtime.perception_state.screen_capture_fail_count);
    const double cap_total = cap_ok + cap_fail;
    sample.capture_success_rate = cap_total > 0.0 ? (cap_ok / cap_total) : 1.0;

    sample.scene_p95_latency_ms = runtime.scene_p95_latency_ms;
    sample.ocr_p95_latency_ms = runtime.ocr_p95_latency_ms;
    sample.face_p95_latency_ms = runtime.face_p95_latency_ms;

    const double ocr_runs = static_cast<double>(std::max<std::int64_t>(1, runtime.perception_state.ocr_total_runs));
    sample.ocr_timeout_rate = static_cast<double>(runtime.perception_state.ocr_error_info.degraded_count) / ocr_runs;
    sample.asr_timeout_rate = runtime.asr_timeout_rate;
    sample.plugin_timeout_rate = runtime.plugin_timeout_rate;

    if (runtime.runtime_metrics_series_capacity == 0) {
        runtime.runtime_metrics_series.clear();
        runtime.runtime_metrics_series_head = 0;
        runtime.runtime_metrics_series_size = 0;
    } else {
        if (runtime.runtime_metrics_series.size() != runtime.runtime_metrics_series_capacity) {
            runtime.runtime_metrics_series.assign(runtime.runtime_metrics_series_capacity, RuntimeMetricsSample{});
            runtime.runtime_metrics_series_head = 0;
            runtime.runtime_metrics_series_size = 0;
        }
        const std::size_t idx = (runtime.runtime_metrics_series_head + runtime.runtime_metrics_series_size) % runtime.runtime_metrics_series_capacity;
        runtime.runtime_metrics_series[idx] = sample;
        if (runtime.runtime_metrics_series_size < runtime.runtime_metrics_series_capacity) {
            ++runtime.runtime_metrics_series_size;
        } else {
            runtime.runtime_metrics_series_head = (runtime.runtime_metrics_series_head + 1) % runtime.runtime_metrics_series_capacity;
        }
    }

    LogObsInfo("runtime.metrics",
               "SNAPSHOT",
               "app_systems.observability.metrics",
               std::string("seq=") + std::to_string(static_cast<long long>(sample.seq)) +
                   " capture_success_rate=" + std::to_string(sample.capture_success_rate) +
                   " scene_p95_ms=" + std::to_string(sample.scene_p95_latency_ms) +
                   " ocr_p95_ms=" + std::to_string(sample.ocr_p95_latency_ms) +
                   " face_p95_ms=" + std::to_string(sample.face_p95_latency_ms) +
                   " ocr_timeout_rate=" + std::to_string(sample.ocr_timeout_rate) +
                   " asr_timeout_rate=" + std::to_string(sample.asr_timeout_rate) +
                   " plugin_timeout_rate=" + std::to_string(sample.plugin_timeout_rate),
               "runtime-main");
}

}  // namespace

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
                LogObsInfo("reminder",
                           "OK",
                           "app_systems.reminder.poll_due",
                           std::string("due id=") + std::to_string(static_cast<long long>(item.id)) +
                               " title=" + item.title,
                           "runtime-main");
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
                ClearRuntimeError(runtime.reminder_error_info,
                                  "app_systems.reminder.poll",
                                  "poll_success",
                                  "runtime-main");
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

                    // ASR 会话流：纯函数组件更新，便于单测。
                    UpdateAsrSessionState(runtime.asr_last_result, runtime.asr_session_state);
                } else {
                    runtime.asr_last_error = asr_err;
                    RuntimeErrorCode asr_code = result.timeout_detected
                                                    ? RuntimeErrorCode::TimeoutDegraded
                                                    : ClassifyRuntimeErrorCodeFromDetail(asr_err,
                                                                                         RuntimeErrorCode::InferenceFailed);
                    if (asr_code == RuntimeErrorCode::TimeoutDegraded ||
                        asr_code == RuntimeErrorCode::DataQualityDegraded) {
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
                    LogObsError(RuntimeErrorDomainName(runtime.asr_error_info.domain),
                                RuntimeErrorCodeName(runtime.asr_error_info.code),
                                "app_systems.asr.recognize",
                                runtime.asr_error_info.detail,
                                "runtime-main");
                }
            }
        }
    }

    if (runtime.inference_adapter) {
        runtime.inference_adapter->TickHotReload(dt);
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
        runtime.plugin_last_latency_ms = stats.last_latency_ms;
        runtime.plugin_avg_latency_ms = stats.avg_latency_ms;
        runtime.plugin_latency_p50_ms = stats.latency_p50_ms;
        runtime.plugin_latency_p95_ms = stats.latency_p95_ms;
        runtime.plugin_success_rate = stats.success_rate;
        runtime.plugin_current_update_hz = stats.current_update_hz;
        runtime.plugin_auto_disabled = stats.auto_disabled;
        runtime.plugin_last_error = stats.last_error;

        if (stats.auto_disabled) {
            SetPluginError(runtime.plugin_error_info,
                           RuntimeErrorCode::AutoDisabled,
                           stats.last_error.empty() ? std::string("plugin auto disabled") : stats.last_error);
        } else if (stats.internal_error_count > 0 || stats.exception_count > 0 || stats.timeout_count > 0) {
            if (stats.internal_error_count == 0 && stats.exception_count == 0 && stats.timeout_count > 0) {
                SetPluginDegrade(runtime.plugin_error_info,
                                 RuntimeErrorCode::TimeoutDegraded,
                                 stats.last_error.empty() ? std::string("plugin timeout degraded") : stats.last_error);
            } else {
                SetPluginError(runtime.plugin_error_info,
                               RuntimeErrorCode::InternalError,
                               stats.last_error);
            }
        } else {
            ClearRuntimeError(runtime.plugin_error_info,
                              "app_systems.plugin.update",
                              "plugin_worker_healthy",
                              "runtime-main");
        }
    } else {
        runtime.plugin_total_update_count = 0;
        runtime.plugin_timeout_count = 0;
        runtime.plugin_exception_count = 0;
        runtime.plugin_internal_error_count = 0;
        runtime.plugin_disable_count = 0;
        runtime.plugin_recover_count = 0;
        runtime.plugin_timeout_rate = 0.0;
        runtime.plugin_last_latency_ms = 0.0;
        runtime.plugin_avg_latency_ms = 0.0;
        runtime.plugin_latency_p50_ms = 0.0;
        runtime.plugin_latency_p95_ms = 0.0;
        runtime.plugin_success_rate = 0.0;
        runtime.plugin_current_update_hz = 0;
        runtime.plugin_auto_disabled = false;
    }

    runtime.runtime_observability_log_accum_sec += std::max(0.0f, dt);
    if (runtime.runtime_observability_log_enabled &&
        runtime.runtime_observability_log_accum_sec >= std::max(0.2f, runtime.runtime_observability_log_interval_sec)) {
        runtime.runtime_observability_log_accum_sec = 0.0f;

        PushLatency(runtime.scene_latency_window_ms,
                    runtime.runtime_metrics_window_size,
                    runtime.scene_latency_ring_head,
                    runtime.scene_latency_ring_size,
                    static_cast<double>(runtime.perception_state.scene_avg_latency_ms));
        PushLatency(runtime.ocr_latency_window_ms,
                    runtime.runtime_metrics_window_size,
                    runtime.ocr_latency_ring_head,
                    runtime.ocr_latency_ring_size,
                    static_cast<double>(runtime.perception_state.ocr_avg_latency_ms));
        PushLatency(runtime.face_latency_window_ms,
                    runtime.runtime_metrics_window_size,
                    runtime.face_latency_ring_head,
                    runtime.face_latency_ring_size,
                    static_cast<double>(runtime.perception_state.face_avg_latency_ms));

        runtime.scene_p95_latency_ms = ComputeP95(SnapshotRing(runtime.scene_latency_window_ms,
                                                               runtime.scene_latency_ring_head,
                                                               runtime.scene_latency_ring_size));
        runtime.ocr_p95_latency_ms = ComputeP95(SnapshotRing(runtime.ocr_latency_window_ms,
                                                             runtime.ocr_latency_ring_head,
                                                             runtime.ocr_latency_ring_size));
        runtime.face_p95_latency_ms = ComputeP95(SnapshotRing(runtime.face_latency_window_ms,
                                                              runtime.face_latency_ring_head,
                                                              runtime.face_latency_ring_size));

        PushMetricsSample(runtime);

        const auto log_err = [](const RuntimeErrorInfo &err) {
            if (err.code != RuntimeErrorCode::Ok) {
                LogObsError(RuntimeErrorDomainName(err.domain),
                            RuntimeErrorCodeName(err.code),
                            "app_systems.observability.snapshot",
                            std::string("count=") + std::to_string(static_cast<long long>(err.count)) +
                                " degraded=" + std::to_string(static_cast<long long>(err.degraded_count)) +
                                " detail=" + err.detail,
                            "runtime-main");
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

}  // namespace desktoper2D
