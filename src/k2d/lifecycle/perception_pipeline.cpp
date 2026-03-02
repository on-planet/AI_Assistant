#include "k2d/lifecycle/perception_pipeline.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <tuple>
#include <utility>

#include <SDL3/SDL_log.h>

namespace k2d {

bool PerceptionPipeline::Init(PerceptionPipelineState &state, std::string *out_error) {
    state = PerceptionPipelineState{};

    {
        std::string capture_err;
        state.screen_capture_ready = screen_capture_.Init(&capture_err);
        if (!state.screen_capture_ready) {
            state.screen_capture_last_error = capture_err;
            SDL_Log("Screen capture init failed: %s", capture_err.c_str());
        } else {
            SDL_Log("Screen capture init ok: DXGI Desktop Duplication");
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
                SDL_Log("Scene classifier init ok: model=%s labels=%s", pair.first.c_str(), pair.second.c_str());
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
            SDL_Log("Scene classifier init failed: %s", sc_err.c_str());
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
                SDL_Log("OCR init ok: det=%s rec=%s keys=%s",
                        std::get<0>(cand).c_str(),
                        std::get<1>(cand).c_str(),
                        std::get<2>(cand).c_str());
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
            SDL_Log("OCR init failed: %s", ocr_err.c_str());
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
                SDL_Log("CameraFacemesh init ok: model=%s labels=%s", cand.first.c_str(), cand.second.c_str());
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
            SDL_Log("CameraFacemesh init failed: %s", fm_err.c_str());
        }
    }

    if (out_error) {
        out_error->clear();
    }
    return true;
}

void PerceptionPipeline::Shutdown(PerceptionPipelineState &state) noexcept {
    if (ocr_future_.valid()) {
        ocr_future_.wait();
    }
    ocr_running_.store(false, std::memory_order_release);

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
    if (!state.screen_capture_ready) {
        return;
    }

    state.screen_capture_poll_accum_sec += std::max(0.0f, dt);
    if (state.screen_capture_poll_accum_sec < 3.0f) {
        return;
    }

    state.screen_capture_poll_accum_sec = 0.0f;

    ScreenCaptureFrame frame{};
    std::string cap_err;
    if (!screen_capture_.Capture(frame, &cap_err)) {
        if (!cap_err.empty()) {
            state.screen_capture_last_error = cap_err;
        }
        return;
    }

    if (state.scene_classifier_ready) {
        std::string scene_err;
        SceneClassificationResult scene_out{};
        if (scene_classifier_.Classify(frame, scene_out, &scene_err)) {
            state.scene_result = std::move(scene_out);
            state.scene_classifier_last_error.clear();
        } else if (!scene_err.empty()) {
            state.scene_classifier_last_error = scene_err;
        }
    }

    SystemContextSnapshot context_snapshot{};
    std::string ctx_err;
    if (system_context_service_.Capture(context_snapshot, &ctx_err)) {
        state.system_context_snapshot = std::move(context_snapshot);
        state.system_context_last_error.clear();
    } else if (!ctx_err.empty()) {
        state.system_context_last_error = ctx_err;
    }

    if (state.ocr_ready) {
        if (ocr_future_.valid() &&
            ocr_future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            ocr_future_.get();
            ocr_running_.store(false, std::memory_order_release);
        }

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

        if (has_packet) {
            state.ocr_skipped_due_timeout = false;
            if (!packet.ok) {
                if (!packet.error.empty()) {
                    state.ocr_last_error = packet.error;
                }
            } else if (packet.elapsed_ms > state.ocr_timeout_ms) {
                state.ocr_skipped_due_timeout = true;
                state.ocr_last_error = "ocr timeout degrade, keep last stable result";
                if (!state.ocr_last_stable_result.summary.empty() || !state.ocr_last_stable_result.lines.empty()) {
                    state.ocr_result = state.ocr_last_stable_result;
                }
            } else {
                state.ocr_result = std::move(packet.result);
                state.ocr_last_stable_result = state.ocr_result;
                state.ocr_last_error.clear();
            }
        }

        if (!ocr_running_.load(std::memory_order_acquire)) {
            ScreenCaptureFrame ocr_frame = frame;
            OcrSystemContext ocr_context{};
            ocr_context.process_name = state.system_context_snapshot.process_name;
            ocr_context.window_title = state.system_context_snapshot.window_title;
            ocr_context.url = state.system_context_snapshot.url_hint;

            ocr_running_.store(true, std::memory_order_release);
            ocr_future_ = std::async(std::launch::async, [this, ocr_frame = std::move(ocr_frame), ocr_context = std::move(ocr_context)]() mutable {
                AsyncOcrPacket local{};
                local.ready = true;

                const auto t0 = std::chrono::steady_clock::now();
                local.ok = ocr_service_.Recognize(ocr_frame, &ocr_context, local.result, &local.error);
                const auto t1 = std::chrono::steady_clock::now();
                local.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

                std::lock_guard<std::mutex> lk(ocr_mutex_);
                ocr_packet_ = std::move(local);
            });
        }

        std::string norm_summary = state.ocr_result.summary;
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

    if (state.camera_facemesh_ready) {
        FaceEmotionResult face_out{};
        std::string face_err;
        if (camera_facemesh_service_.RecognizeFromCamera(face_out, &face_err)) {
            state.face_emotion_result = std::move(face_out);
            state.camera_facemesh_last_error.clear();
        } else if (!face_err.empty()) {
            state.camera_facemesh_last_error = face_err;
        }

        state.blackboard.face_emotion.face_detected = state.face_emotion_result.face_detected;
        state.blackboard.face_emotion.emotion_label = state.face_emotion_result.emotion_label;
        state.blackboard.face_emotion.emotion_score = state.face_emotion_result.emotion_score;
    }
}

}  // namespace k2d
