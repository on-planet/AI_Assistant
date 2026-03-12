#pragma once

#include <string>
#include <vector>

#include "desktoper2D/capture/screen_capture.h"

namespace desktoper2D {

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

    // 头部姿态（角度制）与眼睛开合度（0~1）
    float head_yaw_deg = 0.0f;
    float head_pitch_deg = 0.0f;
    float head_roll_deg = 0.0f;
    float eye_open_left = 0.0f;
    float eye_open_right = 0.0f;
    float eye_open_avg = 0.0f;
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
    void CancelPending() noexcept;

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

}  // namespace desktoper2D
