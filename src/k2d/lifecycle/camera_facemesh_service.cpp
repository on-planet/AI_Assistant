#include "k2d/lifecycle/camera_facemesh_service.h"

#include <string>

#if K2D_WITH_OPENCV

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

namespace k2d {

struct CameraFaceMeshService::Impl {
    cv::VideoCapture cap;

    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "k2d_camera_facemesh"};
    Ort::SessionOptions sess_opt;
    std::unique_ptr<Ort::Session> session;

    std::string input_name;
    std::string output_name;

    int input_h = 192;
    int input_w = 192;

    int camera_index = 0;
};

bool CameraFaceMeshService::Init(int camera_index,
                                 const std::string &model_path,
                                 std::string *out_error) {
    Shutdown();
    auto impl = std::make_unique<Impl>();
    impl->camera_index = camera_index;

    try {
        impl->cap.open(camera_index, cv::CAP_DSHOW);
        if (!impl->cap.isOpened()) {
            if (!impl->cap.open(camera_index)) {
                if (out_error) *out_error = "open camera failed, index=" + std::to_string(camera_index);
                return false;
            }
        }

        impl->sess_opt.SetIntraOpNumThreads(1);
        impl->sess_opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

#ifdef _WIN32
        std::wstring model_path_w(model_path.begin(), model_path.end());
        impl->session = std::make_unique<Ort::Session>(impl->env, model_path_w.c_str(), impl->sess_opt);
#else
        impl->session = std::make_unique<Ort::Session>(impl->env, model_path.c_str(), impl->sess_opt);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        auto in_name = impl->session->GetInputNameAllocated(0, allocator);
        auto out_name = impl->session->GetOutputNameAllocated(0, allocator);
        impl->input_name = in_name.get();
        impl->output_name = out_name.get();

        auto in_info = impl->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        const auto in_shape = in_info.GetShape();
        if (in_shape.size() == 4) {
            if (in_shape[2] > 0) impl->input_h = static_cast<int>(in_shape[2]);
            if (in_shape[3] > 0) impl->input_w = static_cast<int>(in_shape[3]);
        }
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("camera facemesh init exception: ") + e.what();
        return false;
    }

    impl_ = impl.release();
    return true;
}

void CameraFaceMeshService::Shutdown() noexcept {
    if (!impl_) return;
    if (impl_->cap.isOpened()) {
        impl_->cap.release();
    }
    delete impl_;
    impl_ = nullptr;
}

bool CameraFaceMeshService::IsReady() const noexcept {
    return impl_ && impl_->session && impl_->cap.isOpened();
}

bool CameraFaceMeshService::CaptureAndInfer(FaceMeshResult &out, std::string *out_error) {
    out = FaceMeshResult{};
    if (!IsReady()) {
        if (out_error) *out_error = "camera facemesh service not ready";
        return false;
    }

    cv::Mat bgr;
    if (!impl_->cap.read(bgr) || bgr.empty()) {
        if (out_error) *out_error = "camera frame read failed";
        return false;
    }

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(impl_->input_w, impl_->input_h), 0.0, 0.0, cv::INTER_LINEAR);

    const int h = impl_->input_h;
    const int w = impl_->input_w;
    std::vector<float> input(1ull * 3ull * static_cast<std::size_t>(h) * static_cast<std::size_t>(w), 0.0f);
    const std::size_t plane = static_cast<std::size_t>(h) * static_cast<std::size_t>(w);

    for (int y = 0; y < h; ++y) {
        const auto *row = resized.ptr<std::uint8_t>(y);
        for (int x = 0; x < w; ++x) {
            const std::size_t o = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            const std::uint8_t r = row[x * 3 + 0];
            const std::uint8_t g = row[x * 3 + 1];
            const std::uint8_t b = row[x * 3 + 2];
            input[o] = static_cast<float>(r) / 255.0f;
            input[plane + o] = static_cast<float>(g) / 255.0f;
            input[plane * 2 + o] = static_cast<float>(b) / 255.0f;
        }
    }

    try {
        std::vector<int64_t> shape = {1, 3, h, w};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in_tensor = Ort::Value::CreateTensor<float>(
            mem, input.data(), input.size(), shape.data(), shape.size());

        const char *in_names[] = {impl_->input_name.c_str()};
        const char *out_names[] = {impl_->output_name.c_str()};

        auto out_vals = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &in_tensor, 1, out_names, 1);
        if (out_vals.empty() || !out_vals[0].IsTensor()) {
            if (out_error) *out_error = "facemesh output invalid";
            return false;
        }

        auto info = out_vals[0].GetTensorTypeAndShapeInfo();
        const auto out_shape = info.GetShape();
        const float *ptr = out_vals[0].GetTensorData<float>();
        if (!ptr) {
            if (out_error) *out_error = "facemesh tensor null";
            return false;
        }

        std::size_t n = 0;
        if (out_shape.size() >= 3) {
            n = static_cast<std::size_t>(out_shape[out_shape.size() - 2]);
        } else if (out_shape.size() == 2) {
            n = static_cast<std::size_t>(out_shape[0]);
        }
        if (n == 0) {
            if (out_error) *out_error = "facemesh shape invalid";
            return false;
        }

        out.landmarks.clear();
        out.landmarks.reserve(n);

        const std::size_t stride = 3;
        for (std::size_t i = 0; i < n; ++i) {
            const float x = ptr[i * stride + 0];
            const float y = ptr[i * stride + 1];
            const float z = ptr[i * stride + 2];
            out.landmarks.push_back(FaceLandmark{
                .x = std::clamp(x, 0.0f, 1.0f),
                .y = std::clamp(y, 0.0f, 1.0f),
                .z = z,
            });
        }

        out.face_present = !out.landmarks.empty();
        out.face_presence_score = out.face_present ? 1.0f : 0.0f;

        if (out.landmarks.size() >= 2) {
            const FaceLandmark &l = out.landmarks.front();
            const FaceLandmark &r = out.landmarks.back();
            const float dx = r.x - l.x;
            const float dy = r.y - l.y;
            out.head_roll_deg = std::atan2(dy, std::max(1e-6f, dx)) * 57.2957795f;
        }

        if (!out.landmarks.empty()) {
            float min_x = 1.0f, max_x = 0.0f, min_y = 1.0f, max_y = 0.0f;
            for (const auto &p : out.landmarks) {
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
                min_y = std::min(min_y, p.y);
                max_y = std::max(max_y, p.y);
            }
            const float cx = (min_x + max_x) * 0.5f;
            const float cy = (min_y + max_y) * 0.5f;
            out.head_yaw_deg = std::clamp((0.5f - cx) * 90.0f, -45.0f, 45.0f);
            out.head_pitch_deg = std::clamp((0.5f - cy) * 90.0f, -45.0f, 45.0f);
            const float face_h = std::max(1e-5f, max_y - min_y);
            out.eye_open = std::clamp(0.6f + (0.25f - face_h) * 1.6f, 0.0f, 1.0f);
        }

        if (out_error) out_error->clear();
        return true;
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("facemesh run exception: ") + e.what();
        return false;
    }
}

}  // namespace k2d

#else

namespace k2d {

bool CameraFaceMeshService::Init(int,
                                 const std::string &,
                                 std::string *out_error) {
    if (out_error) *out_error = "OpenCV not found at configure time (K2D_WITH_OPENCV=0)";
    return false;
}

void CameraFaceMeshService::Shutdown() noexcept {}

bool CameraFaceMeshService::IsReady() const noexcept {
    return false;
}

bool CameraFaceMeshService::CaptureAndInfer(FaceMeshResult &out, std::string *out_error) {
    out = FaceMeshResult{};
    if (out_error) *out_error = "OpenCV not enabled";
    return false;
}

}  // namespace k2d

#endif
