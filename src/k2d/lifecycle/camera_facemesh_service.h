#pragma once

#include <string>
#include <vector>

#include "k2d/capture/screen_capture.h"

namespace k2d {

struct FaceKeypoint {
    float x = 0.0f;
    float y = 0.0f;
    float score = 0.0f;
    std::string name;
};

struct FaceEmotionResult {
    bool face_detected = false;
    std::string emotion_label;
    float emotion_score = 0.0f;
    std::vector<float> logits;
    std::vector<FaceKeypoint> keypoints;
};

// 摄像头/人脸表情服务（当前版本）：
// - 使用 OpenCV Haar Cascade 做人脸检测
// - 使用 facemesh.onnx + labels 做表情分类
class CameraFacemeshService {
public:
    bool Init(const std::string &model_path,
              const std::string &labels_path,
              std::string *out_error,
              int camera_index = 0);

    void Shutdown() noexcept;

    bool IsReady() const noexcept;

    bool RecognizeFromFrame(const ScreenCaptureFrame &frame,
                            FaceEmotionResult &out,
                            std::string *out_error);

    bool RecognizeFromCamera(FaceEmotionResult &out,
                             std::string *out_error);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace k2d
