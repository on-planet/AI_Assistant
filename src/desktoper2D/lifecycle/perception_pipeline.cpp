#include "desktoper2D/lifecycle/perception_pipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>

#include "desktoper2D/capture/screen_capture.h"
#include "desktoper2D/core/async_logger.h"
#include "desktoper2D/lifecycle/services/decision_service.h"

namespace desktoper2D {

namespace {

RuntimeErrorCode ClassifyInitErrorCode(const std::string &err) {
    return ClassifyRuntimeErrorCodeFromDetail(err, RuntimeErrorCode::InitFailed);
}

bool ConsumeCadence(const float dt, const float interval_sec, float &accum_sec) {
    accum_sec += std::max(0.0f, dt);
    if (accum_sec < std::max(0.01f, interval_sec)) {
        return false;
    }
    accum_sec = 0.0f;
    return true;
}

bool PerceptionUsesScreenCapture(const PerceptionPipelineState &state) {
    return state.scene_classifier_enabled || state.ocr_enabled;
}

void MarkPerceptionPanelDirty(PerceptionPipelineState &state) {
    state.panel_state_version += 1;
}

template <typename TaskRequest, typename AsyncPacket>
struct AsyncSupervisor {
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> stop{false};
    std::mutex packet_mutex;
    AsyncPacket packet{};
    std::mutex task_mutex;
    std::condition_variable task_cv;
    TaskRequest task{};
    std::atomic<std::uint64_t> submit_seq{0};
    std::uint64_t applied_seq = 0;

    void ResetAsyncState() noexcept {
        {
            std::lock_guard<std::mutex> lk(packet_mutex);
            packet = AsyncPacket{};
        }
        {
            std::lock_guard<std::mutex> lk(task_mutex);
            task = TaskRequest{};
        }
        running.store(false, std::memory_order_release);
        applied_seq = submit_seq.load(std::memory_order_acquire);
    }

    template <typename RunTaskFn>
    void StartWorker(RunTaskFn run_task) {
        if (worker.joinable()) {
            return;
        }

        stop.store(false, std::memory_order_release);
        worker = std::thread([this, run_task = std::move(run_task)]() mutable {
            while (true) {
                TaskRequest req;
                {
                    std::unique_lock<std::mutex> lk(task_mutex);
                    task_cv.wait(lk, [this]() {
                        return stop.load(std::memory_order_acquire) || task.pending;
                    });
                    if (stop.load(std::memory_order_acquire) && !task.pending) {
                        break;
                    }
                    req = std::move(task);
                    task = TaskRequest{};
                }

                AsyncPacket local{};
                local.ready = true;
                local.seq = req.seq;
                run_task(req, local);

                {
                    std::lock_guard<std::mutex> lk(packet_mutex);
                    if (!packet.ready || local.seq >= packet.seq) {
                        packet = std::move(local);
                    }
                }
                running.store(false, std::memory_order_release);
            }
            running.store(false, std::memory_order_release);
        });
    }

    template <typename CancelPendingFn>
    void StopWorker(CancelPendingFn cancel_pending) noexcept {
        stop.store(true, std::memory_order_release);
        cancel_pending();
        {
            std::lock_guard<std::mutex> lk(task_mutex);
            task = TaskRequest{};
        }
        task_cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        ResetAsyncState();
    }

    bool TakeLatestPacket(AsyncPacket &out_packet) {
        std::lock_guard<std::mutex> lk(packet_mutex);
        if (!packet.ready) {
            return false;
        }
        out_packet = std::move(packet);
        packet = AsyncPacket{};
        return true;
    }

    template <typename FillTaskFn>
    void Submit(FillTaskFn fill_task) {
        const std::uint64_t seq = submit_seq.fetch_add(1, std::memory_order_acq_rel) + 1;
        running.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(task_mutex);
            task = TaskRequest{};
            task.pending = true;
            task.seq = seq;
            fill_task(task);
        }
        task_cv.notify_one();
    }
};

}  // namespace

struct PerceptionPipeline::CaptureSupervisor {
    ScreenCapture screen_capture;
    ScreenCaptureFrame frame_cache;

    ~CaptureSupervisor() { screen_capture.Shutdown(); }

    bool Init(PerceptionPipelineState &state, std::string *out_error) {
        std::string capture_err;
        state.screen_capture_ready = screen_capture.Init(&capture_err);
        if (!state.screen_capture_ready) {
            state.screen_capture_last_error = capture_err;
            RecordRuntimeError(state.capture_error_info,
                               RuntimeErrorDomain::PerceptionCapture,
                               RuntimeErrorCode::InitFailed,
                               capture_err);
            LogObsError(RuntimeErrorDomainName(state.capture_error_info.domain),
                        RuntimeErrorCodeName(state.capture_error_info.code),
                        "perception_pipeline.init.capture",
                        state.capture_error_info.detail,
                        "perception-init");
            if (out_error) {
                *out_error = capture_err;
            }
            MarkPerceptionPanelDirty(state);
            return false;
        }

        state.screen_capture_last_error.clear();
        ClearRuntimeError(state.capture_error_info,
                          "perception_pipeline.init.capture",
                          "screen_capture_init_ok",
                          "perception-init");
        LogObsInfo("perception.capture",
                   "OK",
                   "perception_pipeline.init.capture",
                   "screen_capture_init_ok",
                   "perception-init");
        if (out_error) {
            out_error->clear();
        }
        MarkPerceptionPanelDirty(state);
        return true;
    }

    void Shutdown(PerceptionPipelineState &state) noexcept {
        screen_capture.Shutdown();
        state.screen_capture_ready = false;
        frame_cache = ScreenCaptureFrame{};
        MarkPerceptionPanelDirty(state);
    }

    ScreenCaptureFrame *CaptureFrame(float dt, PerceptionPipelineState &state) {
        if (!state.screen_capture_ready || !PerceptionUsesScreenCapture(state)) {
            return nullptr;
        }

        if (!ConsumeCadence(dt, state.screen_capture_poll_interval_sec, state.screen_capture_poll_accum_sec)) {
            return nullptr;
        }

        std::string cap_err;
        if (!screen_capture.Capture(frame_cache, &cap_err)) {
            if (!cap_err.empty()) {
                state.screen_capture_last_error = cap_err;
                RecordRuntimeError(state.capture_error_info,
                                   RuntimeErrorDomain::PerceptionCapture,
                                   RuntimeErrorCode::CaptureFailed,
                                   cap_err);
            }
            state.screen_capture_fail_count += 1;
            MarkPerceptionPanelDirty(state);
            return nullptr;
        }

        state.screen_capture_last_error.clear();
        state.screen_capture_success_count += 1;
        ClearRuntimeError(state.capture_error_info,
                          "perception_pipeline.tick.capture",
                          "capture_frame_ok",
                          "perception-tick");
        MarkPerceptionPanelDirty(state);
        return &frame_cache;
    }
};

struct PerceptionPipeline::SceneSupervisor {
    struct AsyncPacket {
        bool ready = false;
        bool ok = false;
        std::uint64_t seq = 0;
        SceneClassificationResult result;
        std::string error;
        int elapsed_ms = 0;
    };

    struct TaskRequest {
        bool pending = false;
        std::uint64_t seq = 0;
        ScreenCaptureFrame frame;
    };

    SceneClassifier service;
    AsyncSupervisor<TaskRequest, AsyncPacket> async;

    ~SceneSupervisor() {
        StopWorker();
        service.Shutdown();
    }

    void StartWorker() {
        async.StartWorker([this](const TaskRequest &req, AsyncPacket &local) {
            const auto t0 = std::chrono::steady_clock::now();
            local.ok = service.Classify(req.frame, local.result, &local.error);
            const auto t1 = std::chrono::steady_clock::now();
            local.elapsed_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        });
    }

    void StopWorker() noexcept {
        async.StopWorker([this]() { service.CancelPending(); });
    }

    bool Init(PerceptionPipelineState &state,
              const std::vector<std::pair<std::string, std::string>> &scene_model_candidates,
              std::string *out_error) {
        state.scene_classifier_ready = false;

        std::string init_err;
        for (const auto &pair : scene_model_candidates) {
            std::error_code ec_model;
            std::error_code ec_labels;
            if (!std::filesystem::exists(pair.first, ec_model) ||
                !std::filesystem::exists(pair.second, ec_labels)) {
                continue;
            }

            std::string try_err;
            if (service.Init(pair.first, pair.second, &try_err)) {
                state.scene_classifier_ready = true;
                state.scene_classifier_last_error.clear();
                ClearRuntimeError(state.scene_error_info,
                                  "perception_pipeline.init.scene",
                                  "scene_classifier_init_ok",
                                  "perception-init");
                LogObsInfo("perception.scene",
                           "OK",
                           "perception_pipeline.init.scene",
                           std::string("scene_init_ok model=") + pair.first + " labels=" + pair.second,
                           "perception-init");
                StartWorker();
                MarkPerceptionPanelDirty(state);
                if (out_error) {
                    out_error->clear();
                }
                return true;
            }
            if (init_err.empty() && !try_err.empty()) {
                init_err = try_err;
            }
        }

        if (init_err.empty()) {
            init_err = "mobileclip model/labels not found in candidate paths";
        }
        state.scene_classifier_last_error = init_err;
        RecordRuntimeError(state.scene_error_info,
                           RuntimeErrorDomain::PerceptionScene,
                           ClassifyInitErrorCode(init_err),
                           init_err);
        LogObsError(RuntimeErrorDomainName(state.scene_error_info.domain),
                    RuntimeErrorCodeName(state.scene_error_info.code),
                    "perception_pipeline.init.scene",
                    state.scene_error_info.detail,
                    "perception-init");
        MarkPerceptionPanelDirty(state);
        if (out_error) {
            *out_error = init_err;
        }
        return false;
    }

    bool Reload(PerceptionPipelineState &state,
                const std::vector<std::pair<std::string, std::string>> &scene_model_candidates,
                std::string *out_error) {
        StopWorker();
        service.Shutdown();
        return Init(state, scene_model_candidates, out_error);
    }

    void Shutdown(PerceptionPipelineState &state) noexcept {
        StopWorker();
        service.Shutdown();
        state.scene_classifier_ready = false;
        MarkPerceptionPanelDirty(state);
    }

    void Tick(const ScreenCaptureFrame &frame, PerceptionPipelineState &state) {
        if (!state.scene_classifier_enabled || !state.scene_classifier_ready) {
            return;
        }

        AsyncPacket latest_packet;
        const bool has_packet = async.TakeLatestPacket(latest_packet);

        if (has_packet && latest_packet.seq > async.applied_seq) {
            async.applied_seq = latest_packet.seq;
            if (latest_packet.ok) {
                state.scene_total_runs += 1;
                state.scene_total_latency_ms += static_cast<std::int64_t>(std::max(0, latest_packet.elapsed_ms));
                state.scene_avg_latency_ms = state.scene_total_runs > 0
                                                 ? static_cast<float>(static_cast<double>(state.scene_total_latency_ms) /
                                                                      static_cast<double>(state.scene_total_runs))
                                                 : 0.0f;
                state.scene_result = std::move(latest_packet.result);
                PublishTaskDecisionSceneResult(state);
                state.scene_classifier_last_error.clear();
                ClearRuntimeError(state.scene_error_info);
                MarkPerceptionPanelDirty(state);
            } else if (!latest_packet.error.empty()) {
                state.scene_classifier_last_error = latest_packet.error;
                RecordRuntimeError(state.scene_error_info,
                                   RuntimeErrorDomain::PerceptionScene,
                                   RuntimeErrorCode::InferenceFailed,
                                   latest_packet.error);
                MarkPerceptionPanelDirty(state);
            }
        }

        if (!async.running.load(std::memory_order_acquire)) {
            async.Submit([&frame](TaskRequest &task) {
                task.frame = frame;
            });
        }
    }
};

struct PerceptionPipeline::ContextSupervisor {
    SystemContextService service;

    void Tick(float dt, PerceptionPipelineState &state) {
        if (!state.system_context_enabled) {
            return;
        }

        if (!ConsumeCadence(dt, state.system_context_poll_interval_sec, state.system_context_poll_accum_sec)) {
            return;
        }

        SystemContextSnapshot snapshot{};
        std::string capture_err;
        if (service.Capture(snapshot, &capture_err)) {
            state.system_context_snapshot = std::move(snapshot);
            PublishTaskDecisionSystemContext(state);
            state.system_context_last_error.clear();
            ClearRuntimeError(state.system_context_error_info,
                              "perception_pipeline.tick.system_context",
                              "system_context_capture_ok",
                              "perception-tick");
            MarkPerceptionPanelDirty(state);
        } else if (!capture_err.empty()) {
            state.system_context_last_error = capture_err;
            RecordRuntimeError(state.system_context_error_info,
                               RuntimeErrorDomain::PerceptionSystemContext,
                               RuntimeErrorCode::CaptureFailed,
                               capture_err);
            MarkPerceptionPanelDirty(state);
        }
    }
};

struct PerceptionPipeline::OcrSupervisor {
    struct AsyncPacket {
        bool ready = false;
        bool ok = false;
        std::uint64_t seq = 0;
        OcrResult result;
        std::string error;
        int elapsed_ms = 0;
        OcrPerfBreakdown perf{};
    };

    struct TaskRequest {
        bool pending = false;
        std::uint64_t seq = 0;
        ScreenCaptureFrame frame;
        OcrSystemContext context;
    };

    OcrService service;
    OcrPostprocessService postprocess_service;
    AsyncSupervisor<TaskRequest, AsyncPacket> async;

    ~OcrSupervisor() {
        StopWorker();
        service.Shutdown();
    }

    void StartWorker() {
        async.StartWorker([this](const TaskRequest &req, AsyncPacket &local) {
            const auto t0 = std::chrono::steady_clock::now();
            local.ok = service.Recognize(req.frame, &req.context, local.result, &local.error, &local.perf);
            const auto t1 = std::chrono::steady_clock::now();
            local.elapsed_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        });
    }

    void StopWorker() noexcept {
        async.StopWorker([this]() { service.CancelPending(); });
    }

    bool Init(PerceptionPipelineState &state,
              const std::vector<std::tuple<std::string, std::string, std::string>> &ocr_candidates,
              std::string *out_error) {
        state.ocr_ready = false;

        std::string init_err;
        for (const auto &cand : ocr_candidates) {
            std::error_code ec1, ec2, ec3;
            if (!std::filesystem::exists(std::get<0>(cand), ec1) ||
                !std::filesystem::exists(std::get<1>(cand), ec2) ||
                !std::filesystem::exists(std::get<2>(cand), ec3)) {
                continue;
            }

            std::string try_err;
            if (service.Init(std::get<0>(cand), std::get<1>(cand), std::get<2>(cand), &try_err)) {
                state.ocr_ready = true;
                state.ocr_last_error.clear();
                state.ocr_det_input_size = service.GetDetInputSize();
                ClearRuntimeError(state.ocr_error_info,
                                  "perception_pipeline.init.ocr",
                                  "ocr_service_init_ok",
                                  "perception-init");
                LogObsInfo("perception.ocr",
                           "OK",
                           "perception_pipeline.init.ocr",
                           std::string("ocr_init_ok det=") + std::get<0>(cand) + " rec=" + std::get<1>(cand) +
                               " keys=" + std::get<2>(cand),
                           "perception-init");
                StartWorker();
                MarkPerceptionPanelDirty(state);
                if (out_error) {
                    out_error->clear();
                }
                return true;
            }
            if (init_err.empty() && !try_err.empty()) {
                init_err = try_err;
            }
        }

        if (init_err.empty()) {
            init_err = "ppocr det/rec/keys not found in candidate paths";
        }
        state.ocr_last_error = init_err;
        RecordRuntimeError(state.ocr_error_info,
                           RuntimeErrorDomain::PerceptionOcr,
                           ClassifyInitErrorCode(init_err),
                           init_err);
        LogObsError(RuntimeErrorDomainName(state.ocr_error_info.domain),
                    RuntimeErrorCodeName(state.ocr_error_info.code),
                    "perception_pipeline.init.ocr",
                    state.ocr_error_info.detail,
                    "perception-init");
        MarkPerceptionPanelDirty(state);
        if (out_error) {
            *out_error = init_err;
        }
        return false;
    }

    bool Reload(PerceptionPipelineState &state,
                const std::vector<std::tuple<std::string, std::string, std::string>> &ocr_candidates,
                std::string *out_error) {
        StopWorker();
        service.Shutdown();
        return Init(state, ocr_candidates, out_error);
    }

    void Shutdown(PerceptionPipelineState &state) noexcept {
        StopWorker();
        service.Shutdown();
        state.ocr_ready = false;
        MarkPerceptionPanelDirty(state);
    }

    void Tick(const ScreenCaptureFrame &frame, PerceptionPipelineState &state) {
        if (!state.ocr_enabled || !state.ocr_ready) {
            return;
        }

        state.ocr_det_input_size = std::clamp(state.ocr_det_input_size, 160, 1280);
        state.ocr_low_conf_threshold = std::clamp(state.ocr_low_conf_threshold, 0.0f, 1.0f);
        service.SetDetInputSize(state.ocr_det_input_size);

        AsyncPacket latest_packet;
        const bool has_packet = async.TakeLatestPacket(latest_packet);

        bool has_new_packet = false;
        if (has_packet && latest_packet.seq > async.applied_seq) {
            async.applied_seq = latest_packet.seq;
            has_new_packet = true;
            state.ocr_skipped_due_timeout = false;
            if (!latest_packet.ok) {
                if (!latest_packet.error.empty()) {
                    state.ocr_last_error = latest_packet.error;
                    RecordRuntimeError(state.ocr_error_info,
                                       RuntimeErrorDomain::PerceptionOcr,
                                       RuntimeErrorCode::InferenceFailed,
                                       latest_packet.error);
                    MarkPerceptionPanelDirty(state);
                }
            } else {
                state.ocr_total_runs += 1;
                state.ocr_total_latency_ms += static_cast<std::int64_t>(std::max(0, latest_packet.elapsed_ms));
                state.ocr_avg_latency_ms = state.ocr_total_runs > 0
                                               ? static_cast<float>(static_cast<double>(state.ocr_total_latency_ms) /
                                                                    static_cast<double>(state.ocr_total_runs))
                                               : 0.0f;
                state.ocr_preprocess_det_avg_ms = state.ocr_total_runs > 0
                                                      ? static_cast<float>(
                                                            (state.ocr_preprocess_det_avg_ms *
                                                                 static_cast<float>(state.ocr_total_runs - 1) +
                                                             static_cast<float>(latest_packet.perf.preprocess_det_ms)) /
                                                            static_cast<float>(state.ocr_total_runs))
                                                      : 0.0f;
                state.ocr_infer_det_avg_ms = state.ocr_total_runs > 0
                                                 ? static_cast<float>(
                                                       (state.ocr_infer_det_avg_ms *
                                                            static_cast<float>(state.ocr_total_runs - 1) +
                                                        static_cast<float>(latest_packet.perf.infer_det_ms)) /
                                                       static_cast<float>(state.ocr_total_runs))
                                                 : 0.0f;
                state.ocr_preprocess_rec_avg_ms = state.ocr_total_runs > 0
                                                      ? static_cast<float>(
                                                            (state.ocr_preprocess_rec_avg_ms *
                                                                 static_cast<float>(state.ocr_total_runs - 1) +
                                                             static_cast<float>(latest_packet.perf.preprocess_rec_ms)) /
                                                            static_cast<float>(state.ocr_total_runs))
                                                      : 0.0f;
                state.ocr_infer_rec_avg_ms = state.ocr_total_runs > 0
                                                 ? static_cast<float>(
                                                       (state.ocr_infer_rec_avg_ms *
                                                            static_cast<float>(state.ocr_total_runs - 1) +
                                                        static_cast<float>(latest_packet.perf.infer_rec_ms)) /
                                                       static_cast<float>(state.ocr_total_runs))
                                                 : 0.0f;

                OcrResult filtered_result = latest_packet.result;
                const auto raw_lines = latest_packet.result.lines;
                filtered_result.lines.clear();
                filtered_result.lines.reserve(raw_lines.size());

                std::int64_t dropped_low_conf_count = 0;
                for (const auto &line : raw_lines) {
                    const float score = std::clamp(line.score, 0.0f, 1.0f);
                    if (score < 0.5f) {
                        state.ocr_conf_low_count += 1;
                    } else if (score < 0.8f) {
                        state.ocr_conf_mid_count += 1;
                    } else {
                        state.ocr_conf_high_count += 1;
                    }

                    if (score >= state.ocr_low_conf_threshold) {
                        filtered_result.lines.push_back(line);
                    } else {
                        dropped_low_conf_count += 1;
                    }
                }

                state.ocr_total_raw_lines += static_cast<std::int64_t>(raw_lines.size());
                state.ocr_total_kept_lines += static_cast<std::int64_t>(filtered_result.lines.size());
                state.ocr_total_dropped_low_conf_lines += dropped_low_conf_count;
                state.ocr_discard_rate = state.ocr_total_raw_lines > 0
                                             ? static_cast<float>(static_cast<double>(state.ocr_total_dropped_low_conf_lines) /
                                                                  static_cast<double>(state.ocr_total_raw_lines))
                                             : 0.0f;

                if (latest_packet.elapsed_ms > state.ocr_timeout_ms) {
                    state.ocr_skipped_due_timeout = true;
                    state.ocr_last_error = "ocr timeout degrade, keep last stable result";
                    RecordRuntimeDegrade(state.ocr_error_info,
                                         RuntimeErrorDomain::PerceptionOcr,
                                         RuntimeErrorCode::TimeoutDegraded,
                                         state.ocr_last_error);
                    if (!state.ocr_last_stable_result.summary.empty() || !state.ocr_last_stable_result.lines.empty()) {
                        state.ocr_result = state.ocr_last_stable_result;
                    }
                } else {
                    state.ocr_result = std::move(filtered_result);
                    state.ocr_last_stable_result = state.ocr_result;
                    state.ocr_last_error.clear();
                    ClearRuntimeError(state.ocr_error_info,
                                      "perception_pipeline.tick.ocr",
                                      "ocr_inference_ok",
                                      "perception-tick");
                }
                MarkPerceptionPanelDirty(state);
            }
        }

        if (!async.running.load(std::memory_order_acquire)) {
            OcrSystemContext context{};
            context.process_name = state.system_context_snapshot.process_name;
            context.window_title = state.system_context_snapshot.window_title;
            context.url = state.system_context_snapshot.url_hint;

            async.Submit([&frame, &context](TaskRequest &task) {
                task.frame = frame;
                task.context = std::move(context);
            });
        }

        postprocess_service.Apply(state.ocr_result,
                                  state.system_context_snapshot,
                                  state,
                                  has_new_packet);
    }
};

struct PerceptionPipeline::FacemeshSupervisor {
    struct AsyncPacket {
        bool ready = false;
        bool ok = false;
        std::uint64_t seq = 0;
        FaceEmotionResult result;
        std::string error;
        int elapsed_ms = 0;
    };

    struct TaskRequest {
        bool pending = false;
        std::uint64_t seq = 0;
    };

    CameraFacemeshService service;
    AsyncSupervisor<TaskRequest, AsyncPacket> async;

    ~FacemeshSupervisor() {
        StopWorker();
        service.Shutdown();
    }

    void StartWorker() {
        async.StartWorker([this](const TaskRequest &, AsyncPacket &local) {
            const auto t0 = std::chrono::steady_clock::now();
            local.ok = service.RecognizeFromCamera(local.result, &local.error);
            const auto t1 = std::chrono::steady_clock::now();
            local.elapsed_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        });
    }

    void StopWorker() noexcept {
        async.StopWorker([this]() { service.CancelPending(); });
    }

    bool Init(PerceptionPipelineState &state,
              const std::vector<std::pair<std::string, std::string>> &facemesh_candidates,
              std::string *out_error) {
        state.camera_facemesh_ready = false;

        std::string init_err;
        for (const auto &cand : facemesh_candidates) {
            std::error_code ec_model;
            std::error_code ec_labels;
            if (!std::filesystem::exists(cand.first, ec_model) ||
                !std::filesystem::exists(cand.second, ec_labels)) {
                continue;
            }

            std::string try_err;
            if (service.Init(cand.first, cand.second, &try_err, 0)) {
                state.camera_facemesh_ready = true;
                state.camera_facemesh_last_error.clear();
                ClearRuntimeError(state.facemesh_error_info,
                                  "perception_pipeline.init.facemesh",
                                  "facemesh_service_init_ok",
                                  "perception-init");
                LogObsInfo("perception.facemesh",
                           "OK",
                           "perception_pipeline.init.facemesh",
                           std::string("facemesh_init_ok model=") + cand.first + " labels=" + cand.second,
                           "perception-init");
                StartWorker();
                MarkPerceptionPanelDirty(state);
                if (out_error) {
                    out_error->clear();
                }
                return true;
            }
            if (init_err.empty() && !try_err.empty()) {
                init_err = try_err;
            }
        }

        if (init_err.empty()) {
            init_err = "facemesh model/labels not found in candidate paths";
        }
        state.camera_facemesh_last_error = init_err;
        RecordRuntimeError(state.facemesh_error_info,
                           RuntimeErrorDomain::PerceptionFacemesh,
                           ClassifyInitErrorCode(init_err),
                           init_err);
        LogObsError(RuntimeErrorDomainName(state.facemesh_error_info.domain),
                    RuntimeErrorCodeName(state.facemesh_error_info.code),
                    "perception_pipeline.init.facemesh",
                    state.facemesh_error_info.detail,
                    "perception-init");
        MarkPerceptionPanelDirty(state);
        if (out_error) {
            *out_error = init_err;
        }
        return false;
    }

    bool Reload(PerceptionPipelineState &state,
                const std::vector<std::pair<std::string, std::string>> &facemesh_candidates,
                std::string *out_error) {
        StopWorker();
        service.Shutdown();
        return Init(state, facemesh_candidates, out_error);
    }

    void Shutdown(PerceptionPipelineState &state) noexcept {
        StopWorker();
        service.Shutdown();
        state.camera_facemesh_ready = false;
        MarkPerceptionPanelDirty(state);
    }

    void Tick(float dt, PerceptionPipelineState &state) {
        if (!state.camera_facemesh_enabled || !state.camera_facemesh_ready) {
            return;
        }

        AsyncPacket latest_packet;
        const bool has_packet = async.TakeLatestPacket(latest_packet);

        if (has_packet && latest_packet.seq > async.applied_seq) {
            async.applied_seq = latest_packet.seq;
            if (latest_packet.ok) {
                state.face_total_runs += 1;
                state.face_total_latency_ms += static_cast<std::int64_t>(std::max(0, latest_packet.elapsed_ms));
                state.face_avg_latency_ms = state.face_total_runs > 0
                                                ? static_cast<float>(static_cast<double>(state.face_total_latency_ms) /
                                                                     static_cast<double>(state.face_total_runs))
                                                : 0.0f;
                state.face_emotion_result = std::move(latest_packet.result);
                state.camera_facemesh_last_error.clear();
                ClearRuntimeError(state.facemesh_error_info,
                                  "perception_pipeline.tick.facemesh",
                                  "facemesh_inference_ok",
                                  "perception-tick");
                MarkPerceptionPanelDirty(state);
            } else if (!latest_packet.error.empty()) {
                state.camera_facemesh_last_error = latest_packet.error;
                RecordRuntimeError(state.facemesh_error_info,
                                   RuntimeErrorDomain::PerceptionFacemesh,
                                   RuntimeErrorCode::InferenceFailed,
                                   latest_packet.error);
                MarkPerceptionPanelDirty(state);
            }
        }

        const bool cadence_ready =
            ConsumeCadence(dt, state.camera_facemesh_poll_interval_sec, state.camera_facemesh_poll_accum_sec);
        if (cadence_ready && !async.running.load(std::memory_order_acquire)) {
            async.Submit([](TaskRequest &) {});
        }

        state.blackboard.face_emotion.face_detected = state.face_emotion_result.face_detected;
        state.blackboard.face_emotion.emotion_label = state.face_emotion_result.emotion_label;
        state.blackboard.face_emotion.emotion_score = state.face_emotion_result.emotion_score;
    }
};

PerceptionPipeline::PerceptionPipeline()
    : capture_supervisor_(std::make_unique<CaptureSupervisor>()),
      scene_supervisor_(std::make_unique<SceneSupervisor>()),
      ocr_supervisor_(std::make_unique<OcrSupervisor>()),
      context_supervisor_(std::make_unique<ContextSupervisor>()),
      facemesh_supervisor_(std::make_unique<FacemeshSupervisor>()) {}

PerceptionPipeline::~PerceptionPipeline() = default;

bool PerceptionPipeline::Init(PerceptionPipelineState &state,
                              const std::vector<std::pair<std::string, std::string>> &scene_model_candidates,
                              const std::vector<std::tuple<std::string, std::string, std::string>> &ocr_candidates,
                              const std::vector<std::pair<std::string, std::string>> &facemesh_candidates,
                              std::string *out_error) {
    Shutdown(state);
    state = PerceptionPipelineState{};

    std::string first_err;
    std::string err;
    if (!capture_supervisor_->Init(state, &err) && first_err.empty()) {
        first_err = err;
    }
    if (!scene_supervisor_->Init(state, scene_model_candidates, &err) && first_err.empty()) {
        first_err = err;
    }
    if (!ocr_supervisor_->Init(state, ocr_candidates, &err) && first_err.empty()) {
        first_err = err;
    }
    if (!facemesh_supervisor_->Init(state, facemesh_candidates, &err) && first_err.empty()) {
        first_err = err;
    }

    if (out_error) {
        *out_error = first_err;
    }
    return first_err.empty();
}

void PerceptionPipeline::Shutdown(PerceptionPipelineState &state) noexcept {
    facemesh_supervisor_->Shutdown(state);
    ocr_supervisor_->Shutdown(state);
    scene_supervisor_->Shutdown(state);
    capture_supervisor_->Shutdown(state);
}

bool PerceptionPipeline::ReloadSceneClassifier(
    PerceptionPipelineState &state,
    const std::vector<std::pair<std::string, std::string>> &scene_model_candidates,
    std::string *out_error) {
    return scene_supervisor_->Reload(state, scene_model_candidates, out_error);
}

bool PerceptionPipeline::ReloadOcrService(
    PerceptionPipelineState &state,
    const std::vector<std::tuple<std::string, std::string, std::string>> &ocr_candidates,
    std::string *out_error) {
    return ocr_supervisor_->Reload(state, ocr_candidates, out_error);
}

bool PerceptionPipeline::ReloadFacemeshService(
    PerceptionPipelineState &state,
    const std::vector<std::pair<std::string, std::string>> &facemesh_candidates,
    std::string *out_error) {
    return facemesh_supervisor_->Reload(state, facemesh_candidates, out_error);
}

void PerceptionPipeline::Tick(float dt, PerceptionPipelineState &state) {
    context_supervisor_->Tick(dt, state);
    facemesh_supervisor_->Tick(dt, state);

    ScreenCaptureFrame *frame = capture_supervisor_->CaptureFrame(dt, state);
    if (frame == nullptr) {
        return;
    }

    scene_supervisor_->Tick(*frame, state);
    ocr_supervisor_->Tick(*frame, state);
}

}  // namespace desktoper2D

