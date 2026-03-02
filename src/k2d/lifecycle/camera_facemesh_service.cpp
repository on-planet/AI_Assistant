#include "k2d/lifecycle/camera_facemesh_service.h"

#include "k2d/core/json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <cstring>

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>

namespace k2d {
namespace {

std::string ReadTextFile(const std::string &path, std::string *out_error) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) {
        if (out_error) *out_error = "failed to open file: " + path;
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool ParseLabelsJson(const std::string &json_text,
                     std::vector<std::string> &out_labels,
                     std::string *out_error) {
    JsonParseError parse_err{};
    auto root_opt = ParseJson(json_text, &parse_err);
    if (!root_opt) {
        if (out_error) {
            *out_error = "labels json parse failed at line " + std::to_string(parse_err.line) +
                         ", col " + std::to_string(parse_err.column) + ": " + parse_err.message;
        }
        return false;
    }

    const JsonValue *labels_v = root_opt->get("labels");
    if (!labels_v || !labels_v->isArray()) {
        if (out_error) *out_error = "labels json requires array field: labels";
        return false;
    }

    const auto *arr = labels_v->asArray();
    if (!arr || arr->empty()) {
        if (out_error) *out_error = "labels array empty";
        return false;
    }

    out_labels.clear();
    out_labels.reserve(arr->size());
    for (const auto &v : *arr) {
        if (!v.isString()) {
            if (out_error) *out_error = "labels element must be string";
            return false;
        }
        out_labels.push_back(*v.asString());
    }
    return true;
}

}  // namespace

struct CameraFacemeshService::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "k2d_camera_facemesh"};
    Ort::SessionOptions sess_opt;
    std::unique_ptr<Ort::Session> session;

    std::string input_name;
    std::string output_name;

    int input_h = 224;
    int input_w = 224;

    cv::CascadeClassifier face_cascade;
    std::vector<std::string> labels;
    cv::VideoCapture camera;
    int camera_index = 0;
};

bool CameraFacemeshService::Init(const std::string &model_path,
                                 const std::string &labels_path,
                                 std::string *out_error,
                                 int camera_index) {
    Shutdown();

    auto impl = std::make_unique<Impl>();

    std::string labels_text = ReadTextFile(labels_path, out_error);
    if (labels_text.empty()) {
        return false;
    }
    if (!ParseLabelsJson(labels_text, impl->labels, out_error)) {
        return false;
    }

    try {
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
        if (in_shape.size() != 4) {
            if (out_error) *out_error = "facemesh model input shape must be [N,C,H,W]";
            return false;
        }
        if (in_shape[2] > 0) impl->input_h = static_cast<int>(in_shape[2]);
        if (in_shape[3] > 0) impl->input_w = static_cast<int>(in_shape[3]);
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("facemesh init exception: ") + e.what();
        return false;
    }

    // 不能使用 required=true，否则找不到时 OpenCV 会抛异常并触发 terminate。
    std::string cascade_path = cv::samples::findFile("haarcascade_frontalface_default.xml", false, false);
    if (cascade_path.empty()) {
        cascade_path = "external/opencv/sources/data/haarcascades/haarcascade_frontalface_default.xml";
    }
    if (!impl->face_cascade.load(cascade_path)) {
        // 再尝试一组常见相对路径（从构建目录运行时）。
        const std::vector<std::string> fallback_paths = {
            "../external/opencv/sources/data/haarcascades/haarcascade_frontalface_default.xml",
            "../../external/opencv/sources/data/haarcascades/haarcascade_frontalface_default.xml",
            "../../../external/opencv/sources/data/haarcascades/haarcascade_frontalface_default.xml"
        };
        bool loaded = false;
        for (const auto &p : fallback_paths) {
            if (impl->face_cascade.load(p)) {
                loaded = true;
                break;
            }
        }
        if (!loaded) {
            if (out_error) *out_error = "failed to load haarcascade_frontalface_default.xml";
            return false;
        }
    }

    impl->camera_index = camera_index;
    if (!impl->camera.open(camera_index)) {
        if (out_error) *out_error = "failed to open camera index: " + std::to_string(camera_index);
        return false;
    }

    impl_ = impl.release();
    if (out_error) out_error->clear();
    return true;
}

void CameraFacemeshService::Shutdown() noexcept {
    if (!impl_) return;
    if (impl_->camera.isOpened()) {
        impl_->camera.release();
    }
    delete impl_;
    impl_ = nullptr;
}

bool CameraFacemeshService::IsReady() const noexcept {
    return impl_ && impl_->session && impl_->camera.isOpened();
}

bool CameraFacemeshService::RecognizeFromFrame(const ScreenCaptureFrame &frame,
                                               FaceEmotionResult &out,
                                               std::string *out_error) {
    out = FaceEmotionResult{};
    if (!IsReady()) {
        if (out_error) *out_error = "camera facemesh service not ready";
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) {
        if (out_error) *out_error = "invalid frame";
        return false;
    }

    cv::Mat bgra(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t *>(frame.bgra.data()));
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    impl_->face_cascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(40, 40));
    if (faces.empty()) {
        out.face_detected = false;
        if (out_error) out_error->clear();
        return true;
    }

    const auto best_it = std::max_element(faces.begin(), faces.end(), [](const cv::Rect &a, const cv::Rect &b) {
        return a.area() < b.area();
    });
    const cv::Rect face = *best_it;
    out.face_detected = true;

    // 关键点（当前用检测框推导 5-point，后续可替换为真实 facemesh landmark 输出）
    const float x = static_cast<float>(face.x);
    const float y = static_cast<float>(face.y);
    const float w = static_cast<float>(face.width);
    const float h = static_cast<float>(face.height);
    out.keypoints = {
        FaceKeypoint{x + w * 0.30f, y + h * 0.38f, 1.0f, "left_eye"},
        FaceKeypoint{x + w * 0.70f, y + h * 0.38f, 1.0f, "right_eye"},
        FaceKeypoint{x + w * 0.50f, y + h * 0.56f, 1.0f, "nose"},
        FaceKeypoint{x + w * 0.35f, y + h * 0.76f, 1.0f, "mouth_left"},
        FaceKeypoint{x + w * 0.65f, y + h * 0.76f, 1.0f, "mouth_right"},
    };

    cv::Mat roi = bgr(face).clone();
    cv::Mat resized;
    cv::resize(roi, resized, cv::Size(impl_->input_w, impl_->input_h), 0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

    std::vector<float> input(1ull * 3ull * static_cast<std::size_t>(impl_->input_h) * static_cast<std::size_t>(impl_->input_w), 0.0f);
    const std::size_t plane = static_cast<std::size_t>(impl_->input_h) * static_cast<std::size_t>(impl_->input_w);

    for (int yy = 0; yy < impl_->input_h; ++yy) {
        const auto *row = rgb.ptr<cv::Vec3b>(yy);
        for (int xx = 0; xx < impl_->input_w; ++xx) {
            const cv::Vec3b px = row[xx];
            const std::size_t o = static_cast<std::size_t>(yy) * static_cast<std::size_t>(impl_->input_w) + static_cast<std::size_t>(xx);
            input[o] = static_cast<float>(px[0]) / 255.0f;
            input[plane + o] = static_cast<float>(px[1]) / 255.0f;
            input[plane * 2 + o] = static_cast<float>(px[2]) / 255.0f;
        }
    }

    try {
        std::vector<int64_t> shape = {1, 3, impl_->input_h, impl_->input_w};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in_tensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());

        const char *in_names[] = {impl_->input_name.c_str()};
        const char *out_names[] = {impl_->output_name.c_str()};

        auto out_vals = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, &in_tensor, 1, out_names, 1);
        if (out_vals.empty() || !out_vals[0].IsTensor()) {
            if (out_error) *out_error = "facemesh output invalid";
            return false;
        }

        auto out_info = out_vals[0].GetTensorTypeAndShapeInfo();
        const float *ptr = out_vals[0].GetTensorData<float>();
        const std::size_t n = out_info.GetElementCount();
        if (!ptr || n == 0) {
            if (out_error) *out_error = "facemesh output empty";
            return false;
        }

        out.logits.assign(ptr, ptr + n);
        auto it = std::max_element(out.logits.begin(), out.logits.end());
        const std::size_t best = static_cast<std::size_t>(std::distance(out.logits.begin(), it));

        float max_v = *it;
        float sum_exp = 0.0f;
        for (float v : out.logits) sum_exp += std::exp(v - max_v);
        out.emotion_score = (sum_exp > 1e-6f) ? (1.0f / sum_exp) : 0.0f;

        if (best < impl_->labels.size()) {
            out.emotion_label = impl_->labels[best];
        } else {
            out.emotion_label = "unknown";
        }

        if (out_error) out_error->clear();
        return true;
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("facemesh infer exception: ") + e.what();
        return false;
    }
}

bool CameraFacemeshService::RecognizeFromCamera(FaceEmotionResult &out,
                                                std::string *out_error) {
    out = FaceEmotionResult{};
    if (!IsReady()) {
        if (out_error) *out_error = "camera facemesh service not ready";
        return false;
    }

    cv::Mat frame_bgr;
    if (!impl_->camera.read(frame_bgr) || frame_bgr.empty()) {
        if (out_error) *out_error = "failed to read camera frame";
        return false;
    }

    cv::Mat frame_bgra;
    cv::cvtColor(frame_bgr, frame_bgra, cv::COLOR_BGR2BGRA);

    ScreenCaptureFrame fake_frame;
    fake_frame.width = frame_bgra.cols;
    fake_frame.height = frame_bgra.rows;
    const std::size_t bytes = static_cast<std::size_t>(frame_bgra.cols) * static_cast<std::size_t>(frame_bgra.rows) * 4ull;
    fake_frame.bgra.resize(bytes);
    std::memcpy(fake_frame.bgra.data(), frame_bgra.data, bytes);

    return RecognizeFromFrame(fake_frame, out, out_error);
}

}  // namespace k2d
