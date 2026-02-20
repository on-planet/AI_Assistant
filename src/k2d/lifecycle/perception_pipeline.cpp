#include "k2d/lifecycle/perception_pipeline.h"

#include <algorithm>
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
            {"assets/ocr/ppocr_det.onnx", "assets/ocr/ppocr_rec.onnx", "assets/ocr/ppocr_keys.txt"},
            {"../assets/ocr/ppocr_det.onnx", "../assets/ocr/ppocr_rec.onnx", "../assets/ocr/ppocr_keys.txt"},
            {"../../assets/ocr/ppocr_det.onnx", "../../assets/ocr/ppocr_rec.onnx", "../../assets/ocr/ppocr_keys.txt"},
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

    if (out_error) {
        out_error->clear();
    }
    return true;
}

void PerceptionPipeline::Shutdown(PerceptionPipelineState &state) noexcept {
    ocr_service_.Shutdown();
    scene_classifier_.Shutdown();
    screen_capture_.Shutdown();

    state.screen_capture_ready = false;
    state.scene_classifier_ready = false;
    state.ocr_ready = false;
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
        std::string ocr_err;
        OcrResult ocr_out{};
        OcrSystemContext ocr_context{};
        ocr_context.process_name = state.system_context_snapshot.process_name;
        ocr_context.window_title = state.system_context_snapshot.window_title;
        ocr_context.url = state.system_context_snapshot.url_hint;
        if (ocr_service_.Recognize(frame, &ocr_context, ocr_out, &ocr_err)) {
            state.ocr_result = std::move(ocr_out);
            state.ocr_last_error.clear();
        } else if (!ocr_err.empty()) {
            state.ocr_last_error = ocr_err;
        }
    }
}

}  // namespace k2d
