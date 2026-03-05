#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "k2d/capture/screen_capture.h"
#include "k2d/lifecycle/camera_facemesh_service.h"
#include "k2d/lifecycle/ocr_service.h"
#include "k2d/lifecycle/scene_classifier.h"
#include "k2d/lifecycle/system_context_service.h"

#include "k2d/lifecycle/observability/runtime_error_codes.h"

namespace k2d {

struct OcrBlackboardData {
    std::vector<OcrTextLine> lines;
    std::string summary;
    std::vector<std::string> domain_tags;
};

struct FaceEmotionBlackboardData {
    bool face_detected = false;
    std::string emotion_label;
    float emotion_score = 0.0f;
};

struct PerceptionBlackboard {
    OcrBlackboardData ocr;
    FaceEmotionBlackboardData face_emotion;
};

struct PerceptionPipelineState {
    bool enabled = true;
    bool scene_classifier_enabled = true;
    bool ocr_enabled = true;
    bool camera_facemesh_enabled = true;
    bool system_context_enabled = true;

    bool screen_capture_ready = false;
    float screen_capture_poll_accum_sec = 0.0f;
    float screen_capture_poll_interval_sec = 3.0f;
    std::string screen_capture_last_error;
    std::int64_t screen_capture_success_count = 0;
    std::int64_t screen_capture_fail_count = 0;
    RuntimeErrorInfo capture_error_info{};

    bool scene_classifier_ready = false;
    std::string scene_classifier_last_error;
    SceneClassificationResult scene_result;
    std::int64_t scene_total_runs = 0;
    std::int64_t scene_total_latency_ms = 0;
    float scene_avg_latency_ms = 0.0f;
    RuntimeErrorInfo scene_error_info{};

    bool ocr_ready = false;
    std::string ocr_last_error;
    OcrResult ocr_result;
    OcrResult ocr_last_stable_result;
    bool ocr_skipped_due_timeout = false;
    int ocr_timeout_ms = 1500;
    int ocr_det_input_size = 640;
    float ocr_low_conf_threshold = 0.5f;

    std::int64_t ocr_total_runs = 0;
    std::int64_t ocr_total_latency_ms = 0;
    float ocr_avg_latency_ms = 0.0f;
    std::int64_t ocr_total_raw_lines = 0;
    std::int64_t ocr_total_kept_lines = 0;
    std::int64_t ocr_total_dropped_low_conf_lines = 0;
    float ocr_discard_rate = 0.0f;
    std::int64_t ocr_conf_low_count = 0;
    std::int64_t ocr_conf_mid_count = 0;
    std::int64_t ocr_conf_high_count = 0;
    RuntimeErrorInfo ocr_error_info{};

    std::string ocr_summary_candidate;
    std::string ocr_summary_stable;
    int ocr_summary_consistent_count = 0;
    int ocr_summary_debounce_frames = 2;

    SystemContextSnapshot system_context_snapshot;
    std::string system_context_last_error;
    RuntimeErrorInfo system_context_error_info{};

    bool camera_facemesh_ready = false;
    std::string camera_facemesh_last_error;
    FaceEmotionResult face_emotion_result;
    std::int64_t face_total_runs = 0;
    std::int64_t face_total_latency_ms = 0;
    float face_avg_latency_ms = 0.0f;
    RuntimeErrorInfo facemesh_error_info{};

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
    SystemContextService system_context_service_;
    CameraFacemeshService camera_facemesh_service_;

    struct AsyncOcrPacket {
        bool ready = false;
        bool ok = false;
        std::uint64_t seq = 0;
        OcrResult result;
        std::string error;
        int elapsed_ms = 0;
    };

    struct AsyncScenePacket {
        bool ready = false;
        bool ok = false;
        std::uint64_t seq = 0;
        SceneClassificationResult result;
        std::string error;
        int elapsed_ms = 0;
    };

    std::future<void> scene_future_;
    std::atomic<bool> scene_running_{false};
    std::mutex scene_mutex_;
    AsyncScenePacket scene_packet_;

    struct AsyncFacePacket {
        bool ready = false;
        bool ok = false;
        std::uint64_t seq = 0;
        FaceEmotionResult result;
        std::string error;
        int elapsed_ms = 0;
    };

    std::future<void> face_future_;
    std::atomic<bool> face_running_{false};
    std::mutex face_mutex_;
    AsyncFacePacket face_packet_;

    std::future<void> ocr_future_;
    std::atomic<bool> ocr_running_{false};
    std::mutex ocr_mutex_;
    AsyncOcrPacket ocr_packet_;

    std::atomic<std::uint64_t> scene_submit_seq_{0};
    std::atomic<std::uint64_t> ocr_submit_seq_{0};
    std::atomic<std::uint64_t> face_submit_seq_{0};

    std::uint64_t scene_applied_seq_ = 0;
    std::uint64_t ocr_applied_seq_ = 0;
    std::uint64_t face_applied_seq_ = 0;
};

}  // namespace k2d
