#pragma once

#include <string>
#include <vector>

#include "k2d/capture/screen_capture.h"
#include "k2d/lifecycle/ocr_service.h"
#include "k2d/lifecycle/scene_classifier.h"
#include "k2d/lifecycle/system_context_service.h"

namespace k2d {

struct OcrBlackboardData {
    std::vector<OcrTextLine> lines;
    std::string summary;
    std::vector<std::string> domain_tags;
};

struct PerceptionBlackboard {
    OcrBlackboardData ocr;
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
    int ocr_timeout_ms = 120;

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
    SystemContextService system_context_service_;
};

}  // namespace k2d
