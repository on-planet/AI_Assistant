#include "desktoper2D/lifecycle/systems/app_systems.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <deque>
#include <exception>
#include <string>

#include "desktoper2D/core/async_logger.h"
#include "desktoper2D/lifecycle/asr/asr_provider.h"
#include "desktoper2D/lifecycle/asr/asr_session_service.h"
#include "desktoper2D/lifecycle/asr/vad_segmenter.h"
#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/state/runtime_audio_state_ops.h"

namespace desktoper2D {

namespace {

bool ApplyPluginStatsSnapshot(PluginRuntimeState &plugin, const PluginWorkerStats &stats) {
    bool changed = false;
    auto assign = [&changed](auto &dst, const auto &src) {
        if (dst != src) {
            dst = src;
            changed = true;
        }
    };

    assign(plugin.total_update_count, stats.total_update_count);
    assign(plugin.timeout_count, stats.timeout_count);
    assign(plugin.exception_count, stats.exception_count);
    assign(plugin.internal_error_count, stats.internal_error_count);
    assign(plugin.disable_count, stats.disable_count);
    assign(plugin.recover_count, stats.recover_count);
    assign(plugin.timeout_rate, stats.timeout_rate);
    assign(plugin.last_latency_ms, stats.last_latency_ms);
    assign(plugin.avg_latency_ms, stats.avg_latency_ms);
    assign(plugin.latency_p50_ms, stats.latency_p50_ms);
    assign(plugin.latency_p95_ms, stats.latency_p95_ms);
    assign(plugin.success_rate, stats.success_rate);
    assign(plugin.current_update_hz, stats.current_update_hz);
    assign(plugin.auto_disabled, stats.auto_disabled);
    assign(plugin.last_error, stats.last_error);

    if (stats.auto_disabled) {
        SetPluginError(plugin.error_info,
                       RuntimeErrorCode::AutoDisabled,
                       stats.last_error.empty() ? std::string("plugin auto disabled") : stats.last_error);
    } else if (stats.internal_error_count > 0 || stats.exception_count > 0 || stats.timeout_count > 0) {
        if (stats.internal_error_count == 0 && stats.exception_count == 0 && stats.timeout_count > 0) {
            SetPluginDegrade(plugin.error_info,
                             RuntimeErrorCode::TimeoutDegraded,
                             stats.last_error.empty() ? std::string("plugin timeout degraded") : stats.last_error);
        } else {
            SetPluginError(plugin.error_info,
                           RuntimeErrorCode::InternalError,
                           stats.last_error);
        }
    } else {
        ClearRuntimeError(plugin.error_info,
                          "app_systems.plugin.update",
                          "plugin_worker_healthy",
                          "runtime-main");
    }
    if (changed) {
        plugin.panel_state_version += 1;
    }
    return changed;
}

double ComputeP95InPlace(std::vector<double> &samples) {
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

void SnapshotRingInto(const std::vector<double> &ring,
                      std::size_t head,
                      std::size_t size,
                      std::vector<double> &out) {
    out.clear();
    if (ring.empty() || size == 0) {
        return;
    }
    out.reserve(size);
    const std::size_t cap = ring.size();
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(ring[(head + i) % cap]);
    }
}

void PushMetricsSample(PerceptionStateSlice perception, PluginStateSlice plugin_state, OpsStateSlice ops) {
    auto &obs = ops.observability.observability;
    auto &perception_state = perception.perception.perception_state;
    auto &plugin_runtime = plugin_state.plugin.plugin;
    RuntimeMetricsSample sample{};
    sample.ts_ms = ObsNowTsMs();
    sample.seq = ++obs.runtime_metrics_seq;

    const double cap_ok = static_cast<double>(perception_state.screen_capture_success_count);
    const double cap_fail = static_cast<double>(perception_state.screen_capture_fail_count);
    const double cap_total = cap_ok + cap_fail;
    sample.capture_success_rate = cap_total > 0.0 ? (cap_ok / cap_total) : 1.0;

    sample.scene_p95_latency_ms = obs.scene_p95_latency_ms;
    sample.ocr_p95_latency_ms = obs.ocr_p95_latency_ms;
    sample.face_p95_latency_ms = obs.face_p95_latency_ms;

    const double ocr_runs = static_cast<double>(std::max<std::int64_t>(1, perception_state.ocr_total_runs));
    sample.ocr_timeout_rate = static_cast<double>(perception_state.ocr_error_info.degraded_count) / ocr_runs;
    sample.asr_timeout_rate = ops.asr_chat.asr_timeout_rate;
    sample.plugin_timeout_rate = plugin_runtime.timeout_rate;

    if (obs.runtime_metrics_series_capacity == 0) {
        obs.runtime_metrics_series.clear();
        obs.runtime_metrics_series_head = 0;
        obs.runtime_metrics_series_size = 0;
    } else {
        if (obs.runtime_metrics_series.size() != obs.runtime_metrics_series_capacity) {
            obs.runtime_metrics_series.assign(obs.runtime_metrics_series_capacity, RuntimeMetricsSample{});
            obs.runtime_metrics_series_head = 0;
            obs.runtime_metrics_series_size = 0;
        }
        const std::size_t idx = (obs.runtime_metrics_series_head + obs.runtime_metrics_series_size) %
                                obs.runtime_metrics_series_capacity;
        obs.runtime_metrics_series[idx] = sample;
        if (obs.runtime_metrics_series_size < obs.runtime_metrics_series_capacity) {
            ++obs.runtime_metrics_series_size;
        } else {
            obs.runtime_metrics_series_head = (obs.runtime_metrics_series_head + 1) % obs.runtime_metrics_series_capacity;
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

void EnsureAsrWorkerStarted(OpsStateSlice ops) {
    auto &async_state = ops.asr_chat.asr_async_state;
    std::lock_guard<std::mutex> lk(async_state.mutex);
    if (async_state.worker.joinable()) {
        return;
    }

    async_state.stop_requested = false;
    async_state.worker_busy = false;

    auto *provider = &ops.asr_chat.asr_provider;
    auto *provider_mutex = &ops.asr_chat.asr_provider_mutex;
    auto *provider_generation = &ops.asr_chat.asr_provider_generation;
    auto *worker_state = &async_state;
    async_state.worker = std::thread([provider, provider_mutex, provider_generation, worker_state]() {
        while (true) {
            AsrAsyncRequest request{};
            {
                std::unique_lock<std::mutex> lk(worker_state->mutex);
                worker_state->cv.wait(
                    lk,
                    [worker_state]() { return worker_state->stop_requested || !worker_state->request_queue.empty(); });
                if (worker_state->stop_requested && worker_state->request_queue.empty()) {
                    break;
                }

                request = std::move(worker_state->request_queue.front());
                worker_state->request_queue.pop_front();
                worker_state->worker_busy = true;
            }

            AsrAsyncResultPacket packet{};
            packet.seq = request.seq;
            packet.provider_generation = request.provider_generation;
            packet.audio_sec = request.chunk.sample_rate_hz > 0
                                   ? static_cast<double>(request.chunk.samples.size()) /
                                         static_cast<double>(request.chunk.sample_rate_hz)
                                   : 0.0;

            bool produced_result = false;
            {
                std::lock_guard<std::mutex> provider_lock(*provider_mutex);
                if (*provider && request.provider_generation == *provider_generation) {
                    produced_result = true;
                    try {
                        packet.ok = (*provider)->Recognize(request.chunk, request.options, packet.result, &packet.error);
                    } catch (const std::exception &e) {
                        packet.ok = false;
                        packet.error = std::string("asr worker exception: ") + e.what();
                    } catch (...) {
                        packet.ok = false;
                        packet.error = "asr worker exception: unknown";
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lk(worker_state->mutex);
                worker_state->worker_busy = false;
                if (produced_result && !worker_state->stop_requested) {
                    if (worker_state->max_completed_queue_size > 0 &&
                        worker_state->completed_queue.size() >= worker_state->max_completed_queue_size) {
                        worker_state->completed_queue.pop_front();
                    }
                    worker_state->completed_queue.push_back(std::move(packet));
                }
            }
        }
    });
}

void ApplyAsrPacket(PerceptionStateSlice perception, OpsStateSlice ops, AsrAsyncResultPacket packet) {
    ops.asr_chat.asr_total_segments += 1;
    const double infer_sec = static_cast<double>(std::max<std::int64_t>(0, packet.result.latency_ms)) / 1000.0;
    ops.asr_chat.asr_audio_total_sec += packet.audio_sec;
    ops.asr_chat.asr_infer_total_sec += infer_sec;
    ops.asr_chat.asr_rtf = ops.asr_chat.asr_audio_total_sec > 1e-9
                               ? (ops.asr_chat.asr_infer_total_sec / ops.asr_chat.asr_audio_total_sec)
                               : 0.0;

    if (packet.result.timeout_detected) {
        ops.asr_chat.asr_timeout_segments += 1;
    }
    if (packet.result.cloud_attempted) {
        ops.asr_chat.asr_cloud_attempts += 1;
    }
    if (packet.result.cloud_succeeded) {
        ops.asr_chat.asr_cloud_success += 1;
    }
    if (packet.result.fallback_to_offline) {
        ops.asr_chat.asr_cloud_fallbacks += 1;
    }

    ops.asr_chat.asr_timeout_rate = ops.asr_chat.asr_total_segments > 0
                                        ? static_cast<double>(ops.asr_chat.asr_timeout_segments) /
                                              static_cast<double>(ops.asr_chat.asr_total_segments)
                                        : 0.0;
    ops.asr_chat.asr_cloud_call_ratio = ops.asr_chat.asr_total_segments > 0
                                            ? static_cast<double>(ops.asr_chat.asr_cloud_attempts) /
                                                  static_cast<double>(ops.asr_chat.asr_total_segments)
                                            : 0.0;
    ops.asr_chat.asr_cloud_success_ratio = ops.asr_chat.asr_cloud_attempts > 0
                                               ? static_cast<double>(ops.asr_chat.asr_cloud_success) /
                                                     static_cast<double>(ops.asr_chat.asr_cloud_attempts)
                                               : 0.0;
    ops.asr_chat.asr_wer_proxy = std::clamp(1.0 - static_cast<double>(packet.result.confidence), 0.0, 1.0);
    ops.asr_chat.asr_last_switch_reason = packet.result.switch_reason;

    if (packet.ok) {
        ops.asr_chat.asr_last_result = std::move(packet.result);
        ops.asr_chat.asr_last_error.clear();
        UpdateAsrSessionState(ops.asr_chat.asr_last_result, ops.asr_chat.asr_session_state);
        PublishAsrSessionToPerception(ops.asr_chat.asr_session_state, perception.perception.perception_state);
        ops.asr_chat.panel_state_version += 1;
        return;
    }

    const std::string detail = !packet.error.empty()
                                   ? packet.error
                                   : (!packet.result.error.empty() ? packet.result.error : std::string("asr recognize failed"));
    ops.asr_chat.asr_last_error = detail;
    RuntimeErrorCode asr_code = packet.result.timeout_detected
                                    ? RuntimeErrorCode::TimeoutDegraded
                                    : ClassifyRuntimeErrorCodeFromDetail(detail, RuntimeErrorCode::InferenceFailed);
    if (asr_code == RuntimeErrorCode::TimeoutDegraded ||
        asr_code == RuntimeErrorCode::DataQualityDegraded) {
        UpdateRuntimeDegrade(ops.observability.asr_error_info,
                             RuntimeErrorDomain::Asr,
                             asr_code,
                             detail);
    } else {
        UpdateRuntimeError(ops.observability.asr_error_info,
                           RuntimeErrorDomain::Asr,
                           asr_code,
                           detail);
    }
    LogObsError(RuntimeErrorDomainName(ops.observability.asr_error_info.domain),
                RuntimeErrorCodeName(ops.observability.asr_error_info.code),
                "app_systems.asr.recognize",
                ops.observability.asr_error_info.detail,
                "runtime-main");
    ops.asr_chat.panel_state_version += 1;
}

void DrainCompletedAsrPackets(PerceptionStateSlice perception, OpsStateSlice ops) {
    std::deque<AsrAsyncResultPacket> completed;
    {
        std::lock_guard<std::mutex> lk(ops.asr_chat.asr_async_state.mutex);
        completed.swap(ops.asr_chat.asr_async_state.completed_queue);
    }

    std::uint64_t current_generation = 0;
    {
        std::lock_guard<std::mutex> provider_lock(ops.asr_chat.asr_provider_mutex);
        current_generation = ops.asr_chat.asr_provider_generation;
    }
    while (!completed.empty()) {
        AsrAsyncResultPacket packet = std::move(completed.front());
        completed.pop_front();
        if (packet.provider_generation != current_generation) {
            continue;
        }
        ApplyAsrPacket(perception, ops, std::move(packet));
    }
}

void EnqueueAsrRecognition(OpsStateSlice ops, AsrAudioChunk chunk, AsrRecognitionOptions options) {
    auto &async_state = ops.asr_chat.asr_async_state;

    AsrAsyncRequest request{};
    request.chunk = std::move(chunk);
    request.options = std::move(options);
    {
        std::lock_guard<std::mutex> provider_lock(ops.asr_chat.asr_provider_mutex);
        request.provider_generation = ops.asr_chat.asr_provider_generation;
    }

    {
        std::lock_guard<std::mutex> lk(async_state.mutex);
        request.seq = ++async_state.next_request_seq;
        if (async_state.max_request_queue_size > 0 &&
            async_state.request_queue.size() >= async_state.max_request_queue_size) {
            async_state.request_queue.pop_front();
            async_state.dropped_request_count += 1;
        }
        async_state.request_queue.push_back(std::move(request));
    }
    async_state.cv.notify_one();
}

void TickAsrCapture(OpsStateSlice ops, float dt) {
    if (!(ops.perception.feature_flags.asr_enabled && ops.asr_chat.asr_ready && ops.asr_chat.asr_provider)) {
        return;
    }

    EnsureAsrWorkerStarted(ops);

    ops.asr_chat.asr_poll_accum_sec += std::max(0.0f, dt);
    if (ops.asr_chat.asr_poll_accum_sec < 0.02f) {
        return;
    }
    ops.asr_chat.asr_poll_accum_sec = 0.0f;

    const std::size_t frame_samples = static_cast<std::size_t>(std::max(0, ops.asr_chat.asr_frame_samples));
    if (ops.asr_chat.asr_audio_buffer_capacity != frame_samples) {
        ops.asr_chat.asr_audio_buffer_capacity = frame_samples;
        ops.asr_chat.asr_audio_buffer.clear();
        ops.asr_chat.asr_audio_buffer.reserve(ops.asr_chat.asr_audio_buffer_capacity);
    }
    ConsumeMicPcmSamples(ops.core.audio_state,
                         frame_samples,
                         &ops.asr_chat.asr_audio_buffer);
    if (ops.asr_chat.asr_audio_buffer.size() < frame_samples) {
        ops.asr_chat.asr_audio_buffer.resize(frame_samples, 0.0f);
    }

    VadFrame frame{};
    frame.sample_rate_hz = 16000;
    frame.samples = ops.asr_chat.asr_audio_buffer;

    VadSegment seg{};
    if (!ops.asr_chat.asr_vad.Accept(frame, seg)) {
        return;
    }

    AsrAudioChunk chunk{};
    chunk.sample_rate_hz = seg.sample_rate_hz;
    chunk.is_final = seg.is_final;
    chunk.samples = std::move(seg.samples);

    AsrRecognitionOptions options{};
    options.language = "zh";

    EnqueueAsrRecognition(ops, std::move(chunk), std::move(options));
}

}  // namespace

void TickAppSystems(PerceptionStateSlice perception, PluginStateSlice plugin_state, OpsStateSlice ops, float dt) {
    auto &perception_state = perception.perception.perception_state;
    auto &plugin = plugin_state.plugin.plugin;
    auto &obs = ops.observability.observability;

    perception_state.scene_classifier_enabled = perception.perception.feature_flags.scene_classifier_enabled;
    perception_state.ocr_enabled = perception.perception.feature_flags.ocr_enabled;
    perception_state.camera_facemesh_enabled = perception.perception.feature_flags.face_emotion_enabled;
    perception_state.system_context_enabled =
        perception.perception.feature_flags.scene_classifier_enabled || perception.perception.feature_flags.ocr_enabled ||
        perception.perception.feature_flags.face_emotion_enabled;
    perception_state.enabled = perception_state.system_context_enabled;

    if (ops.service.reminder_ready) {
        ops.service.reminder_poll_accum_sec += std::max(0.0f, dt);
        if (ops.service.reminder_poll_accum_sec >= 1.0f) {
            ops.service.reminder_poll_accum_sec = 0.0f;
            const std::int64_t now_sec = static_cast<std::int64_t>(std::time(nullptr));

            std::string due_err;
            auto due_items = ops.service.reminder_service.PollDueAndMarkNotified(now_sec, 8, &due_err);
            if (!due_err.empty()) {
                ops.service.reminder_last_error = due_err;
                UpdateRuntimeError(ops.observability.reminder_error_info,
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
            ops.service.reminder_upcoming = ops.service.reminder_service.ListUpcoming(now_sec, 8, &list_err);
            if (!list_err.empty()) {
                ops.service.reminder_last_error = list_err;
                UpdateRuntimeError(ops.observability.reminder_error_info,
                                   RuntimeErrorDomain::Reminder,
                                   RuntimeErrorCode::InternalError,
                                   list_err);
            } else if (due_err.empty()) {
                ClearRuntimeError(ops.observability.reminder_error_info,
                                  "app_systems.reminder.poll",
                                  "poll_success",
                                  "runtime-main");
            }
        }
    }

    perception.perception.perception_pipeline.Tick(dt, perception_state);
    DrainCompletedAsrPackets(perception, ops);
    TickAsrCapture(ops, dt);

    if (plugin.inference_adapter) {
        plugin.inference_adapter->TickHotReload(dt);
    }

    if (plugin.ready && plugin.inference_adapter) {
        plugin.stats_poll_accum_sec += std::max(0.0f, dt);
        const float stats_poll_interval_sec = std::clamp(plugin.stats_poll_interval_sec, 0.2f, 0.5f);
        if (plugin.stats_poll_accum_sec >= stats_poll_interval_sec) {
            plugin.stats_poll_accum_sec = 0.0f;
            const PluginWorkerStats stats = plugin.inference_adapter->GetStats();
            ApplyPluginStatsSnapshot(plugin, stats);
        }
    } else {
        bool plugin_stats_changed = false;
        auto reset = [&plugin_stats_changed](auto &field, const auto &value) {
            if (field != value) {
                field = value;
                plugin_stats_changed = true;
            }
        };
        plugin.stats_poll_accum_sec = plugin.stats_poll_interval_sec;
        reset(plugin.total_update_count, 0ull);
        reset(plugin.timeout_count, 0ull);
        reset(plugin.exception_count, 0ull);
        reset(plugin.internal_error_count, 0ull);
        reset(plugin.disable_count, 0ull);
        reset(plugin.recover_count, 0ull);
        reset(plugin.timeout_rate, 0.0);
        reset(plugin.last_latency_ms, 0.0);
        reset(plugin.avg_latency_ms, 0.0);
        reset(plugin.latency_p50_ms, 0.0);
        reset(plugin.latency_p95_ms, 0.0);
        reset(plugin.success_rate, 0.0);
        reset(plugin.current_update_hz, 0);
        reset(plugin.auto_disabled, false);
        if (plugin_stats_changed) {
            plugin.panel_state_version += 1;
        }
    }

    obs.log_accum_sec += std::max(0.0f, dt);
    if (obs.log_enabled && obs.log_accum_sec >= std::max(0.2f, obs.log_interval_sec)) {
        obs.log_accum_sec = 0.0f;

        PushLatency(obs.scene_latency_window_ms,
                    obs.runtime_metrics_window_size,
                    obs.scene_latency_ring_head,
                    obs.scene_latency_ring_size,
                    static_cast<double>(perception_state.scene_avg_latency_ms));
        PushLatency(obs.ocr_latency_window_ms,
                    obs.runtime_metrics_window_size,
                    obs.ocr_latency_ring_head,
                    obs.ocr_latency_ring_size,
                    static_cast<double>(perception_state.ocr_avg_latency_ms));
        PushLatency(obs.face_latency_window_ms,
                    obs.runtime_metrics_window_size,
                    obs.face_latency_ring_head,
                    obs.face_latency_ring_size,
                    static_cast<double>(perception_state.face_avg_latency_ms));

        SnapshotRingInto(obs.scene_latency_window_ms,
                         obs.scene_latency_ring_head,
                         obs.scene_latency_ring_size,
                         obs.latency_p95_scratch_ms);
        obs.scene_p95_latency_ms = ComputeP95InPlace(obs.latency_p95_scratch_ms);

        SnapshotRingInto(obs.ocr_latency_window_ms,
                         obs.ocr_latency_ring_head,
                         obs.ocr_latency_ring_size,
                         obs.latency_p95_scratch_ms);
        obs.ocr_p95_latency_ms = ComputeP95InPlace(obs.latency_p95_scratch_ms);

        SnapshotRingInto(obs.face_latency_window_ms,
                         obs.face_latency_ring_head,
                         obs.face_latency_ring_size,
                         obs.latency_p95_scratch_ms);
        obs.face_p95_latency_ms = ComputeP95InPlace(obs.latency_p95_scratch_ms);

        PushMetricsSample(perception, plugin_state, ops);

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

        log_err(perception_state.capture_error_info);
        log_err(perception_state.scene_error_info);
        log_err(perception_state.ocr_error_info);
        log_err(perception_state.system_context_error_info);
        log_err(perception_state.facemesh_error_info);
        log_err(plugin.error_info);
        log_err(ops.observability.asr_error_info);
        log_err(ops.observability.chat_error_info);
        log_err(ops.observability.reminder_error_info);
    }
}

void ShutdownAppSystems(OpsStateSlice ops) noexcept {
    auto &async_state = ops.asr_chat.asr_async_state;
    {
        std::lock_guard<std::mutex> lk(async_state.mutex);
        async_state.stop_requested = true;
        async_state.request_queue.clear();
        async_state.completed_queue.clear();
    }
    async_state.cv.notify_all();
    if (async_state.worker.joinable()) {
        async_state.worker.join();
    }
    {
        std::lock_guard<std::mutex> lk(async_state.mutex);
        async_state.stop_requested = false;
        async_state.worker_busy = false;
        async_state.request_queue.clear();
        async_state.completed_queue.clear();
    }
}

}  // namespace desktoper2D
