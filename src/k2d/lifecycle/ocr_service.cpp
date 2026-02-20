#include "k2d/lifecycle/ocr_service.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <numeric>
#include <sstream>
#include <utility>

#include <onnxruntime_cxx_api.h>

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

std::vector<std::string> ReadKeys(const std::string &path, std::string *out_error) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) {
        if (out_error) *out_error = "failed to open keys file: " + path;
        return {};
    }
    std::vector<std::string> keys;
    std::string line;
    while (std::getline(ifs, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) keys.push_back(line);
    }
    if (keys.empty() && out_error) *out_error = "ocr keys is empty";
    return keys;
}

}  // namespace

struct OcrService::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "k2d_ocr_service"};
    Ort::SessionOptions sess_opt;

    std::unique_ptr<Ort::Session> det_session;
    std::unique_ptr<Ort::Session> rec_session;

    std::string det_input_name;
    std::string det_output_name;

    std::string rec_input_name;
    std::string rec_output_name;

    int rec_h = 48;
    int rec_w = 320;

    std::vector<std::string> rec_keys;
};

bool OcrService::Init(const std::string &det_model_path,
                      const std::string &rec_model_path,
                      const std::string &keys_path,
                      std::string *out_error) {
    Shutdown();
    auto impl = std::make_unique<Impl>();

    impl->rec_keys = ReadKeys(keys_path, out_error);
    if (impl->rec_keys.empty()) {
        return false;
    }

    try {
        impl->sess_opt.SetIntraOpNumThreads(1);
        impl->sess_opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

#ifdef _WIN32
        std::wstring det_path_w(det_model_path.begin(), det_model_path.end());
        std::wstring rec_path_w(rec_model_path.begin(), rec_model_path.end());
        impl->det_session = std::make_unique<Ort::Session>(impl->env, det_path_w.c_str(), impl->sess_opt);
        impl->rec_session = std::make_unique<Ort::Session>(impl->env, rec_path_w.c_str(), impl->sess_opt);
#else
        impl->det_session = std::make_unique<Ort::Session>(impl->env, det_model_path.c_str(), impl->sess_opt);
        impl->rec_session = std::make_unique<Ort::Session>(impl->env, rec_model_path.c_str(), impl->sess_opt);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        {
            auto in_name = impl->det_session->GetInputNameAllocated(0, allocator);
            auto out_name = impl->det_session->GetOutputNameAllocated(0, allocator);
            impl->det_input_name = in_name.get();
            impl->det_output_name = out_name.get();
        }
        {
            auto in_name = impl->rec_session->GetInputNameAllocated(0, allocator);
            auto out_name = impl->rec_session->GetOutputNameAllocated(0, allocator);
            impl->rec_input_name = in_name.get();
            impl->rec_output_name = out_name.get();

            auto in_info = impl->rec_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
            const auto in_shape = in_info.GetShape();
            if (in_shape.size() == 4) {
                if (in_shape[2] > 0) impl->rec_h = static_cast<int>(in_shape[2]);
                if (in_shape[3] > 0) impl->rec_w = static_cast<int>(in_shape[3]);
            }
        }
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("ocr init exception: ") + e.what();
        return false;
    }

    impl_ = impl.release();
    return true;
}

void OcrService::Shutdown() noexcept {
    if (!impl_) return;
    delete impl_;
    impl_ = nullptr;
}

bool OcrService::IsReady() const noexcept {
    return impl_ && impl_->det_session && impl_->rec_session;
}

bool OcrService::Recognize(const ScreenCaptureFrame &frame,
                           const OcrSystemContext *context,
                           OcrResult &out,
                           std::string *out_error) {
    out = OcrResult{};
    if (!IsReady()) {
        if (out_error) *out_error = "ocr service not ready";
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) {
        if (out_error) *out_error = "invalid frame";
        return false;
    }

    // V1 最小落地：先跑 det session 做链路验证；rec 结构也已加载。
    // 后续可在此补充 DB 后处理 + 文本框裁切 + CTC 解码。
    const int det_h = 640;
    const int det_w = 640;
    std::vector<float> det_input(1ull * 3ull * static_cast<std::size_t>(det_h) * static_cast<std::size_t>(det_w), 0.0f);

    auto sample_rgb01 = [&](int x, int y, int c) -> float {
        x = std::clamp(x, 0, frame.width - 1);
        y = std::clamp(y, 0, frame.height - 1);
        const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) + static_cast<std::size_t>(x)) * 4ull;
        const std::uint8_t b = frame.bgra[idx + 0];
        const std::uint8_t g = frame.bgra[idx + 1];
        const std::uint8_t r = frame.bgra[idx + 2];
        if (c == 0) return static_cast<float>(r) / 255.0f;
        if (c == 1) return static_cast<float>(g) / 255.0f;
        return static_cast<float>(b) / 255.0f;
    };

    const std::size_t plane = static_cast<std::size_t>(det_h) * static_cast<std::size_t>(det_w);
    for (int y = 0; y < det_h; ++y) {
        const float syf = (static_cast<float>(y) + 0.5f) * static_cast<float>(frame.height) / static_cast<float>(det_h) - 0.5f;
        const int sy = static_cast<int>(std::round(syf));
        for (int x = 0; x < det_w; ++x) {
            const float sxf = (static_cast<float>(x) + 0.5f) * static_cast<float>(frame.width) / static_cast<float>(det_w) - 0.5f;
            const int sx = static_cast<int>(std::round(sxf));
            const std::size_t o = static_cast<std::size_t>(y) * static_cast<std::size_t>(det_w) + static_cast<std::size_t>(x);
            det_input[o] = sample_rgb01(sx, sy, 0);
            det_input[plane + o] = sample_rgb01(sx, sy, 1);
            det_input[plane * 2 + o] = sample_rgb01(sx, sy, 2);
        }
    }

    try {
        std::vector<int64_t> det_shape = {1, 3, det_h, det_w};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value det_tensor = Ort::Value::CreateTensor<float>(mem,
                                                                 det_input.data(),
                                                                 det_input.size(),
                                                                 det_shape.data(),
                                                                 det_shape.size());

        const char *in_names[] = {impl_->det_input_name.c_str()};
        const char *out_names[] = {impl_->det_output_name.c_str()};
        auto det_out = impl_->det_session->Run(Ort::RunOptions{nullptr}, in_names, &det_tensor, 1, out_names, 1);
        if (det_out.empty() || !det_out[0].IsTensor()) {
            if (out_error) *out_error = "ocr det output invalid";
            return false;
        }

        // V1: 输出占位摘要，确认 det+rec 模型均已加载可用于后续升级。
        out.summary = "OCR pipeline ready (det+rec loaded, det inference ok)";
        if (context) {
            if (!context->process_name.empty()) {
                out.summary += " | process=" + context->process_name;
            }
            if (!context->window_title.empty()) {
                out.summary += " | title=" + context->window_title;
            }
            if (!context->url.empty()) {
                out.summary += " | url=" + context->url;
            }
        }
        out.lines.push_back(OcrTextLine{.text = out.summary, .score = 0.01f});
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("ocr recognize exception: ") + e.what();
        return false;
    }

    return true;
}

}  // namespace k2d
