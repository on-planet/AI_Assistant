#pragma once

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "k2d/capture/screen_capture.h"
#include "k2d/lifecycle/camera_facemesh_service.h"
#include "k2d/lifecycle/ocr_service.h"
#include "k2d/lifecycle/scene_classifier.h"
#include "k2d/lifecycle/system_context_service.h"

namespace k2d {

struct OcrBlackboardData {
    std::vector<OcrTextLine> lines;
    std::string summary;
    std::vector<std::string> domain_tags;
};

struct CameraBlackboardData {
    bool face_present = false;
    float face_presence_score = 0.0f;
    float head_yaw_deg = 0.0f;
    float head_pitch_deg = 0.0f;
    float head_roll_deg = 0.0f;
    float eye_open = 0.0f;
};

struct PerceptionBlackboard {
    OcrBlackboardData ocr;
    CameraBlackboardData camera;
};

struct PerceptionPipelineState {
    bool screen_capture_ready = false;
    float screen_capture_poll_accum_sec = 0.0f;
    std::string screen_capture_last_error;

    bool scene_classifier_ready = false;
    std::string scene_classifier_last_error;
    SceneClassificationResult scene_result;

    bool ocr_ready = false;
    std::string ocr_last_error;
    OcrResult ocr_result;
    OcrResult ocr_last_stable_result;
    bool ocr_skipped_due_timeout = false;
    int ocr_timeout_ms = 1500;
    int ocr_det_input_size = 640;

    std::string ocr_summary_candidate;
    std::string ocr_summary_stable;
    int ocr_summary_consistent_count = 0;
    int ocr_summary_debounce_frames = 2;

    bool camera_ready = false;
    std::string camera_last_error;
    FaceMeshResult camera_result;
    int camera_index = 0;

    SystemContextSnapshot system_context_snapshot;
    std::string system_context_last_error;

    PerceptionBlackboard blackboard;
};

class PerceptionPipeline {
public:
    bool Init(PerceptionPipelineState &state, std::string *out_error);
    void Shutdown(PerceptionPipelineState &state) noexcept;

    void Tick(float dt, PerceptionPipelineState &state);

private:
    ScreenCapture screen_capture_;
    SceneClassifier scene_classifier_;
    OcrService ocr_service_;
    CameraFaceMeshService camera_facemesh_service_;
    SystemContextService system_context_service_;

    struct AsyncOcrPacket {
        bool ready = false;
        bool ok = false;
        OcrResult result;
        std::string error;
        int elapsed_ms = 0;
    };

    std::future<void> ocr_future_;
    std::atomic<bool> ocr_running_{false};
    std::mutex ocr_mutex_;
    AsyncOcrPacket ocr_packet_;
};

}  // namespace k2d
