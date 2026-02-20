#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "k2d/capture/screen_capture.h"

namespace k2d {

struct SceneClassificationResult {
    std::string label;
    float score = 0.0f;
    std::vector<float> logits;
};

class SceneClassifier {
public:
    bool Init(const std::string &model_path,
              const std::string &labels_path,
              std::string *out_error);
    void Shutdown() noexcept;

    bool IsReady() const noexcept;

    bool Classify(const ScreenCaptureFrame &frame,
                  SceneClassificationResult &out,
                  std::string *out_error);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace k2d
