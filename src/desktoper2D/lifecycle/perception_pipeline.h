#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "desktoper2D/lifecycle/camera_facemesh_service.h"
#include "desktoper2D/lifecycle/ocr_service.h"
#include "desktoper2D/lifecycle/scene_classifier.h"
#include "desktoper2D/lifecycle/system_context_service.h"
#include "desktoper2D/lifecycle/services/ocr_postprocess_service.h"

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"

namespace desktoper2D {

struct OcrBlackboardData {
    std::vector<OcrTextLine> lines;
    std::string summary;
    std::vector<std::string> domain_tags;
};

struct AsrBlackboardData {
    std::deque<std::string> utterances;
    std::string session_text;
};

struct FaceEmotionBlackboardData {
    bool face_detected = false;
    std::string emotion_label;
    float emotion_score = 0.0f;
};

struct TaskDecisionSignatureState {
    std::uint64_t system_context_signature = 0;
    std::uint64_t ocr_signature = 0;
    std::uint64_t scene_signature = 0;
    std::uint64_t asr_signature = 0;
    std::uint64_t input_signature = 0;
    OcrResult cached_ocr_input;
    bool ocr_uses_blackboard = false;
    bool system_context_dirty = true;
    bool ocr_dirty = true;
    bool scene_dirty = true;
    bool asr_dirty = true;
    bool input_dirty = true;
};

struct PerceptionBlackboard {
    OcrBlackboardData ocr;
    AsrBlackboardData asr;
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
    float ocr_preprocess_det_avg_ms = 0.0f;
    float ocr_infer_det_avg_ms = 0.0f;
    float ocr_preprocess_rec_avg_ms = 0.0f;
    float ocr_infer_rec_avg_ms = 0.0f;
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
    TaskDecisionSignatureState decision_signature{};
};

class PerceptionPipeline {
public:
    PerceptionPipeline();
    ~PerceptionPipeline();

    bool Init(PerceptionPipelineState &state,
              const std::vector<std::pair<std::string, std::string>> &scene_model_candidates,
              const std::vector<std::tuple<std::string, std::string, std::string>> &ocr_candidates,
              const std::vector<std::pair<std::string, std::string>> &facemesh_candidates,
              std::string *out_error);
    void Shutdown(PerceptionPipelineState &state) noexcept;
    bool ReloadSceneClassifier(PerceptionPipelineState &state,
                               const std::vector<std::pair<std::string, std::string>> &scene_model_candidates,
                               std::string *out_error);
    bool ReloadOcrService(PerceptionPipelineState &state,
                          const std::vector<std::tuple<std::string, std::string, std::string>> &ocr_candidates,
                          std::string *out_error);
    bool ReloadFacemeshService(PerceptionPipelineState &state,
                               const std::vector<std::pair<std::string, std::string>> &facemesh_candidates,
                               std::string *out_error);

    void Tick(float dt, PerceptionPipelineState &state);

private:
    struct CaptureSupervisor;
    struct SceneSupervisor;
    struct OcrSupervisor;
    struct ContextSupervisor;
    struct FacemeshSupervisor;

    std::unique_ptr<CaptureSupervisor> capture_supervisor_;
    std::unique_ptr<SceneSupervisor> scene_supervisor_;
    std::unique_ptr<OcrSupervisor> ocr_supervisor_;
    std::unique_ptr<ContextSupervisor> context_supervisor_;
    std::unique_ptr<FacemeshSupervisor> facemesh_supervisor_;
};

}  // namespace desktoper2D
