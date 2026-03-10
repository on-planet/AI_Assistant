#include "desktoper2D/lifecycle/ocr_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>
#include <numeric>
#include <sstream>
#include <utility>

#include <onnxruntime_cxx_api.h>
#include <opencv2/imgproc.hpp>

namespace desktoper2D {
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

std::vector<std::string> BuildFallbackKeys() {
    // 仅用于 keys 文件缺失时兜底，保证 det+rec 链路可运行。
    // 顺序采用常见可见 ASCII，覆盖英文/数字/基础符号。
    static const std::string kAscii =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
        " ";

    std::vector<std::string> keys;
    keys.reserve(kAscii.size());
    for (char ch : kAscii) {
        keys.emplace_back(1, ch);
    }
    return keys;
}

std::vector<std::string> ReadKeys(const std::string &path, std::string *out_error) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    std::vector<std::string> keys;

    if (ifs) {
        std::string line;
        while (std::getline(ifs, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (!line.empty()) keys.push_back(line);
        }
        if (!keys.empty()) {
            if (out_error) out_error->clear();
            return keys;
        }
    }

    // keys 文件缺失或为空时，回退到内置字符表，避免 OCR 初始化失败。
    keys = BuildFallbackKeys();
    if (out_error) {
        *out_error = "keys file missing/empty, fallback builtin keys: " + path;
    }
    return keys;
}

struct DetBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

std::string DecodeCtc(const float *logits,
                      std::size_t t,
                      std::size_t c,
                      const std::vector<std::string> &keys,
                      float &out_score) {
    out_score = 0.0f;
    if (!logits || t == 0 || c == 0) return {};

    std::string text;
    int prev = -1;
    int valid = 0;
    float score_sum = 0.0f;

    for (std::size_t ti = 0; ti < t; ++ti) {
        const float *row = logits + ti * c;
        std::size_t best = 0;
        float best_v = row[0];
        for (std::size_t ci = 1; ci < c; ++ci) {
            if (row[ci] > best_v) {
                best_v = row[ci];
                best = ci;
            }
        }
        const int cls = static_cast<int>(best);
        if (cls == 0) {
            prev = cls;
            continue;
        }
        if (cls == prev) continue;

        const int key_idx = cls - 1;
        if (key_idx >= 0 && key_idx < static_cast<int>(keys.size())) {
            text += keys[static_cast<std::size_t>(key_idx)];
            score_sum += best_v;
            ++valid;
        }
        prev = cls;
    }

    out_score = valid > 0 ? (score_sum / static_cast<float>(valid)) : 0.0f;
    return text;
}

DetBox MakeBoxFromDetPeak(const float *det,
                          std::size_t h,
                          std::size_t w,
                          int src_w,
                          int src_h) {
    if (!det || h == 0 || w == 0 || src_w <= 0 || src_h <= 0) {
        return {0, 0, std::max(1, src_w), std::max(1, src_h)};
    }

    std::size_t best_i = 0;
    float best_v = det[0];
    for (std::size_t i = 1; i < h * w; ++i) {
        if (det[i] > best_v) {
            best_v = det[i];
            best_i = i;
        }
    }

    const int py = static_cast<int>(best_i / w);
    const int px = static_cast<int>(best_i % w);

    const float cx = (static_cast<float>(px) + 0.5f) * static_cast<float>(src_w) / static_cast<float>(w);
    const float cy = (static_cast<float>(py) + 0.5f) * static_cast<float>(src_h) / static_cast<float>(h);

    const int bw = std::max(24, src_w / 2);
    const int bh = std::max(24, src_h / 8);
    int x = static_cast<int>(std::round(cx - bw * 0.5f));
    int y = static_cast<int>(std::round(cy - bh * 0.5f));

    x = std::clamp(x, 0, std::max(0, src_w - 1));
    y = std::clamp(y, 0, std::max(0, src_h - 1));

    int rw = std::min(bw, src_w - x);
    int rh = std::min(bh, src_h - y);
    rw = std::max(1, rw);
    rh = std::max(1, rh);
    return {x, y, rw, rh};
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
    int det_input_size = 640;

    // 复用预处理缓冲，减少每次 Recognize 的堆分配抖动。
    std::vector<float> det_input_buffer;
    std::vector<float> rec_input_buffer;

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

void OcrService::SetDetInputSize(int size) {
    if (!impl_) return;
    impl_->det_input_size = std::clamp(size, 160, 1280);
}

int OcrService::GetDetInputSize() const noexcept {
    if (!impl_) return 640;
    return impl_->det_input_size;
}

bool OcrService::Recognize(const ScreenCaptureFrame &frame,
                           const OcrSystemContext *context,
                           OcrResult &out,
                           std::string *out_error,
                           OcrPerfBreakdown *out_perf) {
    out = OcrResult{};
    if (out_perf) {
        *out_perf = OcrPerfBreakdown{};
    }
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
    const int det_h = std::clamp(impl_->det_input_size, 160, 1280);
    const int det_w = det_h;
    const std::size_t det_count = 1ull * 3ull * static_cast<std::size_t>(det_h) * static_cast<std::size_t>(det_w);
    impl_->det_input_buffer.resize(det_count);
    std::fill(impl_->det_input_buffer.begin(), impl_->det_input_buffer.end(), 0.0f);
    auto &det_input = impl_->det_input_buffer;

    cv::Mat bgra(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t *>(frame.bgra.data()));
    cv::Mat rgb;
    cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);

    const auto det_pre_t0 = std::chrono::steady_clock::now();
    cv::Mat det_rgb;
    cv::resize(rgb, det_rgb, cv::Size(det_w, det_h), 0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat det_rgb_f32;
    det_rgb.convertTo(det_rgb_f32, CV_32FC3, 1.0 / 255.0);

    const std::size_t plane = static_cast<std::size_t>(det_h) * static_cast<std::size_t>(det_w);
    for (int y = 0; y < det_h; ++y) {
        const auto *row = det_rgb_f32.ptr<cv::Vec3f>(y);
        for (int x = 0; x < det_w; ++x) {
            const cv::Vec3f px = row[x];
            const std::size_t o = static_cast<std::size_t>(y) * static_cast<std::size_t>(det_w) + static_cast<std::size_t>(x);
            det_input[o] = px[0];
            det_input[plane + o] = px[1];
            det_input[plane * 2 + o] = px[2];
        }
    }

    const auto det_pre_t1 = std::chrono::steady_clock::now();
    if (out_perf) {
        out_perf->preprocess_det_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(det_pre_t1 - det_pre_t0).count());
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
        const auto det_infer_t0 = std::chrono::steady_clock::now();
        auto det_out = impl_->det_session->Run(Ort::RunOptions{nullptr}, in_names, &det_tensor, 1, out_names, 1);
        const auto det_infer_t1 = std::chrono::steady_clock::now();
        if (out_perf) {
            out_perf->infer_det_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(det_infer_t1 - det_infer_t0).count());
        }
        if (det_out.empty() || !det_out[0].IsTensor()) {
            if (out_error) *out_error = "ocr det output invalid";
            return false;
        }

        auto det_info = det_out[0].GetTensorTypeAndShapeInfo();
        const auto det_shape_out = det_info.GetShape();
        const float *det_ptr = det_out[0].GetTensorData<float>();
        if (!det_ptr || det_shape_out.size() < 2) {
            if (out_error) *out_error = "ocr det tensor invalid";
            return false;
        }

        std::size_t map_h = static_cast<std::size_t>(det_shape_out[det_shape_out.size() - 2]);
        std::size_t map_w = static_cast<std::size_t>(det_shape_out[det_shape_out.size() - 1]);
        if (map_h == 0 || map_w == 0) {
            map_h = static_cast<std::size_t>(det_h);
            map_w = static_cast<std::size_t>(det_w);
        }

        DetBox box = MakeBoxFromDetPeak(det_ptr, map_h, map_w, frame.width, frame.height);

        const std::size_t rec_count = 1ull * 3ull * static_cast<std::size_t>(impl_->rec_h) * static_cast<std::size_t>(impl_->rec_w);
        impl_->rec_input_buffer.resize(rec_count);
        std::fill(impl_->rec_input_buffer.begin(), impl_->rec_input_buffer.end(), 0.0f);
        auto &rec_input = impl_->rec_input_buffer;
        const auto rec_pre_t0 = std::chrono::steady_clock::now();
        cv::Rect box_rect(box.x, box.y, box.w, box.h);
        box_rect &= cv::Rect(0, 0, frame.width, frame.height);
        if (box_rect.width <= 0 || box_rect.height <= 0) {
            box_rect = cv::Rect(0, 0, frame.width, frame.height);
        }

        cv::Mat rec_rgb = rgb(box_rect);
        cv::Mat rec_resized;
        cv::resize(rec_rgb, rec_resized, cv::Size(impl_->rec_w, impl_->rec_h), 0.0, 0.0, cv::INTER_LINEAR);

        cv::Mat rec_rgb_f32;
        rec_resized.convertTo(rec_rgb_f32, CV_32FC3, 1.0 / 255.0);

        const std::size_t rec_plane = static_cast<std::size_t>(impl_->rec_h) * static_cast<std::size_t>(impl_->rec_w);
        for (int y = 0; y < impl_->rec_h; ++y) {
            const auto *row = rec_rgb_f32.ptr<cv::Vec3f>(y);
            for (int x = 0; x < impl_->rec_w; ++x) {
                const cv::Vec3f px = row[x];
                const std::size_t o = static_cast<std::size_t>(y) * static_cast<std::size_t>(impl_->rec_w) + static_cast<std::size_t>(x);
                rec_input[o] = px[0];
                rec_input[rec_plane + o] = px[1];
                rec_input[rec_plane * 2 + o] = px[2];
            }
        }

        const auto rec_pre_t1 = std::chrono::steady_clock::now();
        if (out_perf) {
            out_perf->preprocess_rec_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(rec_pre_t1 - rec_pre_t0).count());
        }

        std::vector<int64_t> rec_shape = {1, 3, impl_->rec_h, impl_->rec_w};
        Ort::Value rec_tensor = Ort::Value::CreateTensor<float>(mem,
                                                                 rec_input.data(),
                                                                 rec_input.size(),
                                                                 rec_shape.data(),
                                                                 rec_shape.size());
        const char *rec_in_names[] = {impl_->rec_input_name.c_str()};
        const char *rec_out_names[] = {impl_->rec_output_name.c_str()};
        const auto rec_infer_t0 = std::chrono::steady_clock::now();
        auto rec_out = impl_->rec_session->Run(Ort::RunOptions{nullptr}, rec_in_names, &rec_tensor, 1, rec_out_names, 1);
        const auto rec_infer_t1 = std::chrono::steady_clock::now();
        if (out_perf) {
            out_perf->infer_rec_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(rec_infer_t1 - rec_infer_t0).count());
        }
        if (rec_out.empty() || !rec_out[0].IsTensor()) {
            if (out_error) *out_error = "ocr rec output invalid";
            return false;
        }

        auto rec_info = rec_out[0].GetTensorTypeAndShapeInfo();
        const auto rec_out_shape = rec_info.GetShape();
        const float *rec_ptr = rec_out[0].GetTensorData<float>();
        if (!rec_ptr || rec_out_shape.size() < 2) {
            if (out_error) *out_error = "ocr rec tensor invalid";
            return false;
        }

        std::size_t t = 0;
        std::size_t c = 0;
        if (rec_out_shape.size() == 3) {
            t = static_cast<std::size_t>(rec_out_shape[1]);
            c = static_cast<std::size_t>(rec_out_shape[2]);
        } else {
            t = static_cast<std::size_t>(rec_out_shape[0]);
            c = static_cast<std::size_t>(rec_out_shape[1]);
        }
        if (t == 0 || c == 0) {
            if (out_error) *out_error = "ocr rec shape invalid";
            return false;
        }

        float rec_score = 0.0f;
        std::string rec_text = DecodeCtc(rec_ptr, t, c, impl_->rec_keys, rec_score);
        if (rec_text.empty()) rec_text = "";

        out.lines.push_back(OcrTextLine{
            .text = rec_text,
            .score = rec_score,
            .bbox_x = static_cast<float>(box.x) / static_cast<float>(std::max(1, frame.width)),
            .bbox_y = static_cast<float>(box.y) / static_cast<float>(std::max(1, frame.height)),
            .bbox_w = static_cast<float>(box.w) / static_cast<float>(std::max(1, frame.width)),
            .bbox_h = static_cast<float>(box.h) / static_cast<float>(std::max(1, frame.height)),
        });

        out.summary = "OCR det+rec ok";
        if (context) {
            if (!context->process_name.empty()) out.summary += " | process=" + context->process_name;
            if (!context->window_title.empty()) out.summary += " | title=" + context->window_title;
            if (!context->url.empty()) out.summary += " | url=" + context->url;
        }
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("ocr recognize exception: ") + e.what();
        return false;
    }

    return true;
}

}  // namespace desktoper2D
