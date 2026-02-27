#pragma once

#include <string>
#include <vector>

namespace k2d {

struct FaceLandmark {
    float x = 0.0f;  // [0,1]
    float y = 0.0f;  // [0,1]
    float z = 0.0f;
};

struct FaceMeshResult {
    bool face_present = false;
    float face_presence_score = 0.0f;
    float head_yaw_deg = 0.0f;
    float head_pitch_deg = 0.0f;
    float head_roll_deg = 0.0f;
    float eye_open = 0.0f;
    std::vector<FaceLandmark> landmarks;
};

class CameraFaceMeshService {
public:
    bool Init(int camera_index,
              const std::string &model_path,
              std::string *out_error);

    void Shutdown() noexcept;

    bool IsReady() const noexcept;

    bool CaptureAndInfer(FaceMeshResult &out, std::string *out_error);

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace k2d
