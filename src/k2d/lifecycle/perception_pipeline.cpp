#include "k2d/lifecycle/perception_pipeline.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <tuple>
#include <utility>

#include "k2d/core/async_logger.h"

namespace k2d {

namespace {

RuntimeErrorCode ClassifyInitErrorCode(const std::string &err) {
    return ClassifyRuntimeErrorCodeFromDetail(err, RuntimeErrorCode::InitFailed);
}

}  // namespace

void PerceptionPipeline::StartWorkers() {
    workers_stop_.store(false, std::memory_order_release);

    scene_worker_ = std::thread([this]() {
        while (true) {
            SceneTaskRequest req;
            {
                std::unique_lock<std::mutex> lk(scene_task_mutex_);
                scene_task_cv_.wait(lk, [this]() { return workers_stop_.load(std::memory_order_acquire) || scene_task_.pending; });
                if (workers_stop_.load(std::memory_order_acquire) && !scene_task_.pending) {
                    break;
                }
                req = std::move(scene_task_);
                scene_task_ = SceneTaskRequest{};
            }

            AsyncScenePacket local{};
            local.ready = true;
            local.seq = req.seq;
            const auto t0 = std::chrono::steady_clock::now();
            local.ok = scene_classifier_.Classify(req.frame, local.result, &local.error);
            const auto t1 = std::chrono::steady_clock::now();
            local.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            {
                std::lock_guard<std::mutex> lk(scene_mutex_);
                if (!scene_packet_.ready || local.seq >= scene_packet_.seq) {
                    scene_packet_ = std::move(local);
                }
            }
            scene_running_.store(false, std::memory_order_release);
        }
    });

    ocr_worker_ = std::thread([this]() {
        while (true) {
            OcrTaskRequest req;
            {
                std::unique_lock<std::mutex> lk(ocr_task_mutex_);
                ocr_task_cv_.wait(lk, [this]() { return workers_stop_.load(std::memory_order_acquire) || ocr_task_.pending; });
                if (workers_stop_.load(std::memory_order_acquire) && !ocr_task_.pending) {
                    break;
                }
                req = std::move(ocr_task_);
                ocr_task_ = OcrTaskRequest{};
            }

            AsyncOcrPacket local{};
            local.ready = true;
            local.seq = req.seq;
            const auto t0 = std::chrono::steady_clock::now();
            local.ok = ocr_service_.Recognize(req.frame, &req.context, local.result, &local.error, &local.perf);
            const auto t1 = std::chrono::steady_clock::now();
            local.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            {
                std::lock_guard<std::mutex> lk(ocr_mutex_);
                if (!ocr_packet_.ready || local.seq >= ocr_packet_.seq) {
                    ocr_packet_ = std::move(local);
                }
            }
            ocr_running_.store(false, std::memory_order_release);
        }
    });

    face_worker_ = std::thread([this]() {
        while (true) {
            FaceTaskRequest req;
            {
                std::unique_lock<std::mutex> lk(face_task_mutex_);
                face_task_cv_.wait(lk, [this]() { return workers_stop_.load(std::memory_order_acquire) || face_task_.pending; });
                if (workers_stop_.load(std::memory_order_acquire) && !face_task_.pending) {
                    break;
                }
                req = std::move(face_task_);
                face_task_ = FaceTaskRequest{};
            }

            AsyncFacePacket local{};
            local.ready = true;
            local.seq = req.seq;
            const auto t0 = std::chrono::steady_clock::now();
            local.ok = camera_facemesh_service_.RecognizeFromCamera(local.result, &local.error);
            const auto t1 = std::chrono::steady_clock::now();
            local.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            {
                std::lock_guard<std::mutex> lk(face_mutex_);
                if (!face_packet_.ready || local.seq >= face_packet_.seq) {
                    face_packet_ = std::move(local);
                }
            }
            face_running_.store(false, std::memory_order_release);
        }
    });
}

void PerceptionPipeline::StopWorkers() noexcept {
    workers_stop_.store(true, std::memory_order_release);
    scene_task_cv_.notify_all();
    ocr_task_cv_.notify_all();
    face_task_cv_.notify_all();

    if (scene_worker_.joinable()) scene_worker_.join();
    if (ocr_worker_.joinable()) ocr_worker_.join();
    if (face_worker_.joinable()) face_worker_.join();

    scene_running_.store(false, std::memory_order_release);
    ocr_running_.store(false, std::memory_order_release);
    face_running_.store(false, std::memory_order_release);
}

bool PerceptionPipeline::Init(PerceptionPipelineState &state, std::string *out_error) {
    state = PerceptionPipelineState{};

    {
        std::string capture_err;
        state.screen_capture_ready = screen_capture_.Init(&capture_err);
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
        } else {
            ClearRuntimeError(state.capture_error_info,
                              "perception_pipeline.init.capture",
                              "screen_capture_init_ok",
                              "perception-init");
            LogObsInfo("perception.capture",
                       "OK",
                       "perception_pipeline.init.capture",
                       "screen_capture_init_ok",
                       "perception-init");
        }
    }

    {
        std::string sc_err;
        std::vector<std::pair<std::string, std::string>> scene_model_candidates;
        scene_model_candidates.reserve(24);

#ifdef K2D_PROJECT_DIR
        {
            const std::filesystem::path root = std::filesystem::path(K2D_PROJECT_DIR);
            scene_model_candidates.emplace_back((root / "assets" / "mobileclip_image.onnx").generic_string(),
                                                (root / "assets" / "mobileclip_labels.json").generic_string());
        }
#endif

        {
            std::error_code ec;
            const auto cwd = std::filesystem::current_path(ec);
            if (!ec) {
                scene_model_candidates.emplace_back((cwd / "assets" / "mobileclip_image.onnx").lexically_normal().generic_string(),
                                                    (cwd / "assets" / "mobileclip_labels.json").lexically_normal().generic_string());
            }
        }

        std::string prefix;
        for (int i = 0; i <= 12; ++i) {
            scene_model_candidates.emplace_back(prefix + "assets/mobileclip_image.onnx",
                                                prefix + "assets/mobileclip_labels.json");
            prefix += "../";
        }

        for (const auto &pair : scene_model_candidates) {
            std::error_code ec1;
            std::error_code ec2;
            const bool model_exists = std::filesystem::exists(pair.first, ec1);
            const bool labels_exists = std::filesystem::exists(pair.second, ec2);
            if (!model_exists || !labels_exists) {
                continue;
            }

            std::string try_err;
            if (scene_classifier_.Init(pair.first, pair.second, &try_err)) {
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
                break;
            }
            if (sc_err.empty() && !try_err.empty()) {
                sc_err = try_err;
            }
        }

        if (!state.scene_classifier_ready) {
            if (sc_err.empty()) {
                sc_err = "mobileclip model/labels not found in candidate paths";
            }
            state.scene_classifier_last_error = sc_err;
            RecordRuntimeError(state.scene_error_info,
                               RuntimeErrorDomain::PerceptionScene,
                               ClassifyInitErrorCode(sc_err),
                               sc_err);
            LogObsError(RuntimeErrorDomainName(state.scene_error_info.domain),
                        RuntimeErrorCodeName(state.scene_error_info.code),
                        "perception_pipeline.init.scene",
                        state.scene_error_info.detail,
                        "perception-init");
        }
    }

    {
        std::string ocr_err;
        const std::vector<std::tuple<std::string, std::string, std::string>> ocr_candidates = {
            {"assets/PP-OCRv5_server_det_infer.onnx", "assets/PP-OCRv5_server_rec_infer.onnx", "assets/ocr/ppocr_keys.txt"},
            {"../assets/PP-OCRv5_server_det_infer.onnx", "../assets/PP-OCRv5_server_rec_infer.onnx", "../assets/ocr/ppocr_keys.txt"},
            {"../../assets/PP-OCRv5_server_det_infer.onnx", "../../assets/PP-OCRv5_server_rec_infer.onnx", "../../assets/ocr/ppocr_keys.txt"},
            {"assets/PP-OCRv5_server_det_infer.onnx", "assets/PP-OCRv5_server_rec_infer.onnx", "assets/ppocr_keys.txt"},
            {"../assets/PP-OCRv5_server_det_infer.onnx", "../assets/PP-OCRv5_server_rec_infer.onnx", "../assets/ppocr_keys.txt"},
            {"../../assets/PP-OCRv5_server_det_infer.onnx", "../../assets/PP-OCRv5_server_rec_infer.onnx", "../../assets/ppocr_keys.txt"},
        };

        for (const auto &cand : ocr_candidates) {
            std::error_code ec1, ec2, ec3;
            if (!std::filesystem::exists(std::get<0>(cand), ec1) ||
                !std::filesystem::exists(std::get<1>(cand), ec2) ||
                !std::filesystem::exists(std::get<2>(cand), ec3)) {
                continue;
            }

            std::string try_err;
            if (ocr_service_.Init(std::get<0>(cand), std::get<1>(cand), std::get<2>(cand), &try_err)) {
                state.ocr_ready = true;
                state.ocr_last_error.clear();
                state.ocr_det_input_size = ocr_service_.GetDetInputSize();
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
                break;
            }
            if (ocr_err.empty() && !try_err.empty()) {
                ocr_err = try_err;
            }
        }

        if (!state.ocr_ready) {
            if (ocr_err.empty()) {
                ocr_err = "ppocr det/rec/keys not found in candidate paths";
            }
            state.ocr_last_error = ocr_err;
            RecordRuntimeError(state.ocr_error_info,
                               RuntimeErrorDomain::PerceptionOcr,
                               ClassifyInitErrorCode(ocr_err),
                               ocr_err);
            LogObsError(RuntimeErrorDomainName(state.ocr_error_info.domain),
                        RuntimeErrorCodeName(state.ocr_error_info.code),
                        "perception_pipeline.init.ocr",
                        state.ocr_error_info.detail,
                        "perception-init");
        }
    }

    {
        std::string fm_err;
        const std::vector<std::pair<std::string, std::string>> facemesh_candidates = {
            {"assets/facemesh.onnx", "assets/facemesh.labels.json"},
            {"../assets/facemesh.onnx", "../assets/facemesh.labels.json"},
            {"../../assets/facemesh.onnx", "../../assets/facemesh.labels.json"},
        };

        for (const auto &cand : facemesh_candidates) {
            std::error_code ec1, ec2;
            if (!std::filesystem::exists(cand.first, ec1) || !std::filesystem::exists(cand.second, ec2)) {
                continue;
            }

            std::string try_err;
            if (camera_facemesh_service_.Init(cand.first, cand.second, &try_err, 0)) {
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
                break;
            }
            if (fm_err.empty() && !try_err.empty()) {
                fm_err = try_err;
            }
        }

        if (!state.camera_facemesh_ready) {
            if (fm_err.empty()) {
                fm_err = "facemesh model/labels not found in candidate paths";
            }
            state.camera_facemesh_last_error = fm_err;
            RecordRuntimeError(state.facemesh_error_info,
                               RuntimeErrorDomain::PerceptionFacemesh,
                               ClassifyInitErrorCode(fm_err),
                               fm_err);
            LogObsError(RuntimeErrorDomainName(state.facemesh_error_info.domain),
                        RuntimeErrorCodeName(state.facemesh_error_info.code),
                        "perception_pipeline.init.facemesh",
                        state.facemesh_error_info.detail,
                        "perception-init");
        }
    }

    StartWorkers();

    if (out_error) {
        out_error->clear();
    }
    return true;
}

void PerceptionPipeline::Shutdown(PerceptionPipelineState &state) noexcept {
    StopWorkers();

    ocr_service_.Shutdown();
    scene_classifier_.Shutdown();
    screen_capture_.Shutdown();
    camera_facemesh_service_.Shutdown();

    state.screen_capture_ready = false;
    state.scene_classifier_ready = false;
    state.ocr_ready = false;
    state.camera_facemesh_ready = false;
}

void PerceptionPipeline::Tick(float dt, PerceptionPipelineState &state) {
    if (!state.enabled || !state.screen_capture_ready) {
        return;
    }

    state.screen_capture_poll_accum_sec += std::max(0.0f, dt);
    if (state.screen_capture_poll_accum_sec < std::max(0.1f, state.screen_capture_poll_interval_sec)) {
        return;
    }

    state.screen_capture_poll_accum_sec = 0.0f;

    ScreenCaptureFrame frame{};
    std::string cap_err;
    if (!screen_capture_.Capture(frame, &cap_err)) {
        if (!cap_err.empty()) {
            state.screen_capture_last_error = cap_err;
            RecordRuntimeError(state.capture_error_info,
                               RuntimeErrorDomain::PerceptionCapture,
                               RuntimeErrorCode::CaptureFailed,
                               cap_err);
        }
        state.screen_capture_fail_count += 1;
        return;
    }

    state.screen_capture_last_error.clear();
    state.screen_capture_success_count += 1;
    ClearRuntimeError(state.capture_error_info,
                      "perception_pipeline.tick.capture",
                      "capture_frame_ok",
                      "perception-tick");

    if (state.scene_classifier_enabled && state.scene_classifier_ready) {

        AsyncScenePacket scene_packet;
        bool has_scene_packet = false;
        {
            std::lock_guard<std::mutex> lk(scene_mutex_);
            if (scene_packet_.ready) {
                scene_packet = std::move(scene_packet_);
                scene_packet_ = AsyncScenePacket{};
                has_scene_packet = true;
            }
        }

        if (has_scene_packet && scene_packet.seq > scene_applied_seq_) {
            scene_applied_seq_ = scene_packet.seq;
            if (scene_packet.ok) {
                state.scene_total_runs += 1;
                state.scene_total_latency_ms += static_cast<std::int64_t>(std::max(0, scene_packet.elapsed_ms));
                state.scene_avg_latency_ms = state.scene_total_runs > 0
                                             ? static_cast<float>(static_cast<double>(state.scene_total_latency_ms) /
                                                                  static_cast<double>(state.scene_total_runs))
                                             : 0.0f;
                state.scene_result = std::move(scene_packet.result);
                state.scene_classifier_last_error.clear();
                ClearRuntimeError(state.scene_error_info);
            } else if (!scene_packet.error.empty()) {
                state.scene_classifier_last_error = scene_packet.error;
                RecordRuntimeError(state.scene_error_info,
                                   RuntimeErrorDomain::PerceptionScene,
                                   RuntimeErrorCode::InferenceFailed,
                                   scene_packet.error);
            }
        }

        if (!scene_running_.load(std::memory_order_acquire)) {
            const std::uint64_t seq = scene_submit_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
            scene_running_.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(scene_task_mutex_);
                scene_task_.pending = true;
                scene_task_.seq = seq;
                scene_task_.frame = frame;
            }
            scene_task_cv_.notify_one();
        }
    }

    if (state.system_context_enabled) {
        SystemContextSnapshot context_snapshot{};
        std::string ctx_err;
        if (system_context_service_.Capture(context_snapshot, &ctx_err)) {
            state.system_context_snapshot = std::move(context_snapshot);
            state.system_context_last_error.clear();
            ClearRuntimeError(state.system_context_error_info,
                              "perception_pipeline.tick.system_context",
                              "system_context_capture_ok",
                              "perception-tick");
        } else if (!ctx_err.empty()) {
            state.system_context_last_error = ctx_err;
            RecordRuntimeError(state.system_context_error_info,
                               RuntimeErrorDomain::PerceptionSystemContext,
                               RuntimeErrorCode::CaptureFailed,
                               ctx_err);
        }
    }

    if (state.ocr_enabled && state.ocr_ready) {
        state.ocr_det_input_size = std::clamp(state.ocr_det_input_size, 160, 1280);
        state.ocr_low_conf_threshold = std::clamp(state.ocr_low_conf_threshold, 0.0f, 1.0f);
        ocr_service_.SetDetInputSize(state.ocr_det_input_size);


        AsyncOcrPacket packet;
        bool has_packet = false;
        {
            std::lock_guard<std::mutex> lk(ocr_mutex_);
            if (ocr_packet_.ready) {
                packet = std::move(ocr_packet_);
                ocr_packet_ = AsyncOcrPacket{};
                has_packet = true;
            }
        }

        bool has_new_ocr_packet = false;
        if (has_packet && packet.seq > ocr_applied_seq_) {
            ocr_applied_seq_ = packet.seq;
            has_new_ocr_packet = true;
            state.ocr_skipped_due_timeout = false;
            if (!packet.ok) {
                if (!packet.error.empty()) {
                    state.ocr_last_error = packet.error;
                    RecordRuntimeError(state.ocr_error_info,
                                       RuntimeErrorDomain::PerceptionOcr,
                                       RuntimeErrorCode::InferenceFailed,
                                       packet.error);
                }
            } else {
                state.ocr_total_runs += 1;
                state.ocr_total_latency_ms += static_cast<std::int64_t>(std::max(0, packet.elapsed_ms));
                state.ocr_avg_latency_ms = state.ocr_total_runs > 0
                                            ? static_cast<float>(static_cast<double>(state.ocr_total_latency_ms) /
                                                                 static_cast<double>(state.ocr_total_runs))
                                            : 0.0f;
                state.ocr_preprocess_det_avg_ms = state.ocr_total_runs > 0
                                                       ? static_cast<float>((state.ocr_preprocess_det_avg_ms * static_cast<float>(state.ocr_total_runs - 1) +
                                                                            static_cast<float>(packet.perf.preprocess_det_ms)) /
                                                                           static_cast<float>(state.ocr_total_runs))
                                                       : 0.0f;
                state.ocr_infer_det_avg_ms = state.ocr_total_runs > 0
                                                  ? static_cast<float>((state.ocr_infer_det_avg_ms * static_cast<float>(state.ocr_total_runs - 1) +
                                                                       static_cast<float>(packet.perf.infer_det_ms)) /
                                                                      static_cast<float>(state.ocr_total_runs))
                                                  : 0.0f;
                state.ocr_preprocess_rec_avg_ms = state.ocr_total_runs > 0
                                                       ? static_cast<float>((state.ocr_preprocess_rec_avg_ms * static_cast<float>(state.ocr_total_runs - 1) +
                                                                            static_cast<float>(packet.perf.preprocess_rec_ms)) /
                                                                           static_cast<float>(state.ocr_total_runs))
                                                       : 0.0f;
                state.ocr_infer_rec_avg_ms = state.ocr_total_runs > 0
                                                  ? static_cast<float>((state.ocr_infer_rec_avg_ms * static_cast<float>(state.ocr_total_runs - 1) +
                                                                       static_cast<float>(packet.perf.infer_rec_ms)) /
                                                                      static_cast<float>(state.ocr_total_runs))
                                                  : 0.0f;

                OcrResult filtered_result = packet.result;
                const auto raw_lines = packet.result.lines;
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

                if (packet.elapsed_ms > state.ocr_timeout_ms) {
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
            }
        }

        if (!ocr_running_.load(std::memory_order_acquire)) {
            const std::uint64_t seq = ocr_submit_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
            // 避免在主线程深拷贝整帧 BGRA（可能数 MB~十几 MB），减少 OCR 触发瞬间卡顿。
            ScreenCaptureFrame ocr_frame = std::move(frame);
            OcrSystemContext ocr_context{};
            ocr_context.process_name = state.system_context_snapshot.process_name;
            ocr_context.window_title = state.system_context_snapshot.window_title;
            ocr_context.url = state.system_context_snapshot.url_hint;

            ocr_running_.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(ocr_task_mutex_);
                ocr_task_.pending = true;
                ocr_task_.seq = seq;
                ocr_task_.frame = std::move(ocr_frame);
                ocr_task_.context = std::move(ocr_context);
            }
            ocr_task_cv_.notify_one();
        }

        if (has_new_ocr_packet) {
            std::string norm_summary = state.ocr_result.summary;
            std::transform(norm_summary.begin(), norm_summary.end(), norm_summary.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const std::vector<std::pair<std::string, std::string>> dict = {
                {"visual studio code", "vscode"}, {"vs code", "vscode"}, {"clion", "clion"},
                {"read me", "readme"}, {"help", "help"}, {"settings", "settings"},
                {"file", "file"}, {"edit", "edit"}, {"view", "view"}, {"run", "run"},
                {"debug", "debug"}, {"terminal", "terminal"}, {"browser", "browser"}, {"docs", "docs"}
            };
            std::string norm_text = state.system_context_snapshot.process_name + "\n" +
                                    state.system_context_snapshot.window_title + "\n" +
                                    state.system_context_snapshot.url_hint + "\n" +
                                    norm_summary;
            std::transform(norm_text.begin(), norm_text.end(), norm_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            for (const auto &kv : dict) {
                std::size_t pos = 0;
                while ((pos = norm_text.find(kv.first, pos)) != std::string::npos) {
                    norm_text.replace(pos, kv.first.size(), kv.second);
                    pos += kv.second.size();
                }
                pos = 0;
                while ((pos = norm_summary.find(kv.first, pos)) != std::string::npos) {
                    norm_summary.replace(pos, kv.first.size(), kv.second);
                    pos += kv.second.size();
                }
            }

            if (state.ocr_summary_candidate == norm_summary) {
                state.ocr_summary_consistent_count += 1;
            } else {
                state.ocr_summary_candidate = norm_summary;
                state.ocr_summary_consistent_count = 1;
            }
            if (state.ocr_summary_consistent_count >= std::max(2, state.ocr_summary_debounce_frames)) {
                state.ocr_summary_stable = state.ocr_summary_candidate;
            }

            state.blackboard.ocr.lines = state.ocr_result.lines;
            state.blackboard.ocr.summary = state.ocr_summary_stable.empty() ? state.ocr_result.summary : state.ocr_summary_stable;
            state.blackboard.ocr.domain_tags = state.ocr_result.domain_tags;

            std::vector<std::string> inferred_tags;
            auto push_unique = [&](const std::string &tag) {
                if (std::find(inferred_tags.begin(), inferred_tags.end(), tag) == inferred_tags.end()) {
                    inferred_tags.push_back(tag);
                }
            };

            if (norm_text.find("http") != std::string::npos || norm_text.find("www.") != std::string::npos ||
                norm_text.find("chrome") != std::string::npos || norm_text.find("edge") != std::string::npos ||
                norm_text.find("firefox") != std::string::npos || norm_text.find("browser") != std::string::npos) {
                push_unique("browser");
            }
            if (norm_text.find("chat") != std::string::npos || norm_text.find("discord") != std::string::npos ||
                norm_text.find("slack") != std::string::npos || norm_text.find("wechat") != std::string::npos ||
                norm_text.find("qq") != std::string::npos) {
                push_unique("chat");
            }
            if (norm_text.find("readme") != std::string::npos || norm_text.find("docs") != std::string::npos ||
                norm_text.find("manual") != std::string::npos || norm_text.find("wiki") != std::string::npos ||
                norm_text.find("help") != std::string::npos) {
                push_unique("doc");
            }
            if (norm_text.find("cpp") != std::string::npos || norm_text.find("cmake") != std::string::npos ||
                norm_text.find("visual studio") != std::string::npos || norm_text.find("clion") != std::string::npos ||
                norm_text.find("vscode") != std::string::npos || norm_text.find("github") != std::string::npos ||
                norm_text.find("debug") != std::string::npos || norm_text.find("terminal") != std::string::npos) {
                push_unique("code");
            }

            for (const auto &tag : inferred_tags) {
                if (std::find(state.blackboard.ocr.domain_tags.begin(), state.blackboard.ocr.domain_tags.end(), tag) ==
                    state.blackboard.ocr.domain_tags.end()) {
                    state.blackboard.ocr.domain_tags.push_back(tag);
                }
            }
        }
    }

    if (state.camera_facemesh_enabled && state.camera_facemesh_ready) {

        AsyncFacePacket face_packet;
        bool has_face_packet = false;
        {
            std::lock_guard<std::mutex> lk(face_mutex_);
            if (face_packet_.ready) {
                face_packet = std::move(face_packet_);
                face_packet_ = AsyncFacePacket{};
                has_face_packet = true;
            }
        }

        if (has_face_packet && face_packet.seq > face_applied_seq_) {
            face_applied_seq_ = face_packet.seq;
            if (face_packet.ok) {
                state.face_total_runs += 1;
                state.face_total_latency_ms += static_cast<std::int64_t>(std::max(0, face_packet.elapsed_ms));
                state.face_avg_latency_ms = state.face_total_runs > 0
                                            ? static_cast<float>(static_cast<double>(state.face_total_latency_ms) /
                                                                 static_cast<double>(state.face_total_runs))
                                            : 0.0f;
                state.face_emotion_result = std::move(face_packet.result);
                state.camera_facemesh_last_error.clear();
                ClearRuntimeError(state.facemesh_error_info,
                                  "perception_pipeline.tick.facemesh",
                                  "facemesh_inference_ok",
                                  "perception-tick");
            } else if (!face_packet.error.empty()) {
                state.camera_facemesh_last_error = face_packet.error;
                RecordRuntimeError(state.facemesh_error_info,
                                   RuntimeErrorDomain::PerceptionFacemesh,
                                   RuntimeErrorCode::InferenceFailed,
                                   face_packet.error);
            }
        }

        if (!face_running_.load(std::memory_order_acquire)) {
            const std::uint64_t seq = face_submit_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
            face_running_.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(face_task_mutex_);
                face_task_.pending = true;
                face_task_.seq = seq;
            }
            face_task_cv_.notify_one();
        }

        state.blackboard.face_emotion.face_detected = state.face_emotion_result.face_detected;
        state.blackboard.face_emotion.emotion_label = state.face_emotion_result.emotion_label;
        state.blackboard.face_emotion.emotion_score = state.face_emotion_result.emotion_score;
    }
}

}  // namespace k2d
