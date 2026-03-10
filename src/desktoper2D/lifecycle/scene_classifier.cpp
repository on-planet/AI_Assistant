#include "desktoper2D/lifecycle/scene_classifier.h"

#include "desktoper2D/core/json.h"

#include <algorithm>
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

std::vector<float> Softmax(const std::vector<float> &x) {
    if (x.empty()) return {};
    const float max_v = *std::max_element(x.begin(), x.end());
    std::vector<float> e(x.size(), 0.0f);
    float sum = 0.0f;
    for (std::size_t i = 0; i < x.size(); ++i) {
        e[i] = std::exp(x[i] - max_v);
        sum += e[i];
    }
    if (sum <= 1e-12f) return e;
    for (float &v : e) v /= sum;
    return e;
}

bool ParseLabelsJson(const std::string &json_text,
                     std::vector<std::string> &out_labels,
                     std::vector<std::vector<float>> &out_embeddings,
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
    const JsonValue &root = *root_opt;
    const JsonValue *labels_v = root.get("labels");
    const JsonValue *emb_v = root.get("embeddings");
    if (!labels_v || !labels_v->isArray() || !emb_v || !emb_v->isArray()) {
        if (out_error) *out_error = "labels json requires array fields: labels, embeddings";
        return false;
    }
    const auto *labels_arr = labels_v->asArray();
    const auto *emb_arr = emb_v->asArray();
    if (!labels_arr || !emb_arr || labels_arr->size() != emb_arr->size() || labels_arr->empty()) {
        if (out_error) *out_error = "labels/embeddings size mismatch or empty";
        return false;
    }

    out_labels.clear();
    out_embeddings.clear();
    out_labels.reserve(labels_arr->size());
    out_embeddings.reserve(emb_arr->size());

    std::size_t dim = 0;
    for (std::size_t i = 0; i < labels_arr->size(); ++i) {
        const JsonValue &lv = (*labels_arr)[i];
        const JsonValue &ev = (*emb_arr)[i];
        if (!lv.isString() || !ev.isArray()) {
            if (out_error) *out_error = "labels/embeddings element type invalid";
            return false;
        }
        out_labels.push_back(*lv.asString());

        const auto *vec_arr = ev.asArray();
        if (!vec_arr || vec_arr->empty()) {
            if (out_error) *out_error = "embedding vector empty";
            return false;
        }
        if (dim == 0) dim = vec_arr->size();
        if (vec_arr->size() != dim) {
            if (out_error) *out_error = "embedding dim mismatch";
            return false;
        }

        std::vector<float> vec;
        vec.reserve(vec_arr->size());
        for (const auto &x : *vec_arr) {
            if (!x.isNumber()) {
                if (out_error) *out_error = "embedding must be numeric";
                return false;
            }
            vec.push_back(static_cast<float>(*x.asNumber()));
        }
        out_embeddings.push_back(std::move(vec));
    }
    return true;
}

void L2Normalize(std::vector<float> &v) {
    if (v.empty()) return;
    float sum2 = 0.0f;
    for (float x : v) sum2 += x * x;
    const float n = std::sqrt(std::max(1e-12f, sum2));
    for (float &x : v) x /= n;
}


}  // namespace

struct SceneClassifier::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "k2d_scene_classifier"};
    std::unique_ptr<Ort::Session> session;
    Ort::SessionOptions sess_opt;

    std::string input_name;
    std::vector<int64_t> input_shape;

    std::string output_name;

    std::vector<std::string> labels;
    std::vector<std::vector<float>> label_embeddings;

    int input_h = 224;
    int input_w = 224;

    // 复用预处理缓冲，减少每次 Classify 的堆分配抖动。
    std::vector<float> input_buffer;
};

bool SceneClassifier::Init(const std::string &model_path,
                           const std::string &labels_path,
                           std::string *out_error) {
    Shutdown();
    auto impl = std::make_unique<Impl>();

    std::string labels_text = ReadTextFile(labels_path, out_error);
    if (labels_text.empty()) {
        return false;
    }
    if (!ParseLabelsJson(labels_text, impl->labels, impl->label_embeddings, out_error)) {
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
        impl->input_shape = in_info.GetShape();
        if (impl->input_shape.size() != 4) {
            if (out_error) *out_error = "expect model input shape [N,C,H,W]";
            return false;
        }
        if (impl->input_shape[1] != 3) {
            if (out_error) *out_error = "expect model input channel C=3";
            return false;
        }

        if (impl->input_shape[2] > 0) impl->input_h = static_cast<int>(impl->input_shape[2]);
        if (impl->input_shape[3] > 0) impl->input_w = static_cast<int>(impl->input_shape[3]);

        const auto emb_dim = static_cast<int>(impl->label_embeddings.front().size());
        for (auto &v : impl->label_embeddings) {
            if (static_cast<int>(v.size()) != emb_dim) {
                if (out_error) *out_error = "labels embedding dim mismatch";
                return false;
            }
            L2Normalize(v);
        }
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("scene classifier init exception: ") + e.what();
        return false;
    }

    impl_ = impl.release();
    return true;
}

void SceneClassifier::Shutdown() noexcept {
    if (!impl_) return;
    delete impl_;
    impl_ = nullptr;
}

bool SceneClassifier::IsReady() const noexcept {
    return impl_ && impl_->session;
}

bool SceneClassifier::Classify(const ScreenCaptureFrame &frame,
                               SceneClassificationResult &out,
                               std::string *out_error) {
    if (!IsReady()) {
        if (out_error) *out_error = "scene classifier not ready";
        return false;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) {
        if (out_error) *out_error = "invalid frame";
        return false;
    }

    const int target_h = impl_->input_h;
    const int target_w = impl_->input_w;
    const std::size_t input_count = 1ull * 3ull * static_cast<std::size_t>(target_h) * static_cast<std::size_t>(target_w);
    impl_->input_buffer.resize(input_count);
    std::fill(impl_->input_buffer.begin(), impl_->input_buffer.end(), 0.0f);
    auto &input = impl_->input_buffer;

    cv::Mat bgra(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t *>(frame.bgra.data()));
    cv::Mat rgb;
    cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);

    // 与常见 CLIP 图像预处理对齐：保持比例缩放到短边 256（224 输入时），再中心裁剪到输入尺寸。
    const float resize_scale = 256.0f / 224.0f;
    const int resize_short = std::max(1, static_cast<int>(std::round(static_cast<float>(std::min(target_h, target_w)) * resize_scale)));
    const int src_w = frame.width;
    const int src_h = frame.height;
    const float scale = static_cast<float>(resize_short) / static_cast<float>(std::min(src_w, src_h));
    const int resized_w = std::max(1, static_cast<int>(std::round(static_cast<float>(src_w) * scale)));
    const int resized_h = std::max(1, static_cast<int>(std::round(static_cast<float>(src_h) * scale)));

    cv::Mat resized_rgb;
    cv::resize(rgb, resized_rgb, cv::Size(resized_w, resized_h), 0.0, 0.0, cv::INTER_LINEAR);

    const int crop_x = std::max(0, (resized_w - target_w) / 2);
    const int crop_y = std::max(0, (resized_h - target_h) / 2);
    const int crop_w = std::min(target_w, resized_w - crop_x);
    const int crop_h = std::min(target_h, resized_h - crop_y);
    cv::Rect roi(crop_x, crop_y, crop_w, crop_h);
    cv::Mat cropped_rgb = resized_rgb(roi);

    cv::Mat final_rgb;
    if (cropped_rgb.cols != target_w || cropped_rgb.rows != target_h) {
        cv::resize(cropped_rgb, final_rgb, cv::Size(target_w, target_h), 0.0, 0.0, cv::INTER_LINEAR);
    } else {
        final_rgb = cropped_rgb;
    }

    cv::Mat final_rgb_f32;
    final_rgb.convertTo(final_rgb_f32, CV_32FC3, 1.0 / 255.0);

    constexpr float kMean[3] = {0.48145466f, 0.4578275f, 0.40821073f};
    constexpr float kStd[3] = {0.26862954f, 0.26130258f, 0.27577711f};

    const std::size_t plane = static_cast<std::size_t>(target_h) * static_cast<std::size_t>(target_w);
    for (int y = 0; y < target_h; ++y) {
        const auto *row = final_rgb_f32.ptr<cv::Vec3f>(y);
        for (int x = 0; x < target_w; ++x) {
            const cv::Vec3f px = row[x];
            const std::size_t o0 = static_cast<std::size_t>(y) * static_cast<std::size_t>(target_w) + static_cast<std::size_t>(x);
            input[o0] = (px[0] - kMean[0]) / kStd[0];
            input[plane + o0] = (px[1] - kMean[1]) / kStd[1];
            input[plane * 2 + o0] = (px[2] - kMean[2]) / kStd[2];
        }
    }

    std::vector<int64_t> shape = {1, 3, target_h, target_w};

    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in_tensor = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(), shape.size());

        const char *in_names[] = {impl_->input_name.c_str()};
        const char *out_names[] = {impl_->output_name.c_str()};

        auto out_vals = impl_->session->Run(Ort::RunOptions{nullptr},
                                            in_names,
                                            &in_tensor,
                                            1,
                                            out_names,
                                            1);
        if (out_vals.empty() || !out_vals[0].IsTensor()) {
            if (out_error) *out_error = "invalid model output";
            return false;
        }

        auto out_info = out_vals[0].GetTensorTypeAndShapeInfo();
        const auto out_shape = out_info.GetShape();
        const float *feat = out_vals[0].GetTensorData<float>();
        const std::size_t feat_n = out_info.GetElementCount();
        if (!feat || feat_n == 0) {
            if (out_error) *out_error = "empty output tensor";
            return false;
        }

        std::vector<float> image_embedding;
        if (out_shape.size() == 2 && out_shape[0] == 1) {
            image_embedding.assign(feat, feat + static_cast<std::size_t>(out_shape[1]));
        } else {
            image_embedding.assign(feat, feat + feat_n);
        }

        const std::size_t emb_dim = impl_->label_embeddings.front().size();
        if (image_embedding.size() != emb_dim) {
            if (out_error) {
                *out_error = "embedding dim mismatch, image=" + std::to_string(image_embedding.size()) +
                             ", labels=" + std::to_string(emb_dim);
            }
            return false;
        }

        L2Normalize(image_embedding);

        std::vector<float> logits(impl_->labels.size(), 0.0f);
        for (std::size_t i = 0; i < impl_->labels.size(); ++i) {
            const auto &e = impl_->label_embeddings[i];
            float dot = 0.0f;
            for (std::size_t d = 0; d < emb_dim; ++d) {
                dot += image_embedding[d] * e[d];
            }
            logits[i] = dot;
        }

        const std::vector<float> probs = Softmax(logits);
        const auto it = std::max_element(probs.begin(), probs.end());
        const std::size_t best = static_cast<std::size_t>(std::distance(probs.begin(), it));

        const float best_score = probs[best];

        out.logits = probs;
        out.label = impl_->labels[best];
        out.score = best_score;
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("scene classify exception: ") + e.what();
        return false;
    }

    return true;
}

}  // namespace desktoper2D
