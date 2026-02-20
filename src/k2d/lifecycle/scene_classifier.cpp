#include "k2d/lifecycle/scene_classifier.h"

#include "k2d/core/json.h"

#include <algorithm>
#include <cmath>
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

float SampleBgraBilinearAsRgb01(const ScreenCaptureFrame &frame, float xf, float yf, int c) {
    const float x = std::clamp(xf, 0.0f, static_cast<float>(frame.width - 1));
    const float y = std::clamp(yf, 0.0f, static_cast<float>(frame.height - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, frame.width - 1);
    const int y1 = std::min(y0 + 1, frame.height - 1);

    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    auto get_rgb01 = [&](int px, int py, int ch) -> float {
        const std::size_t idx = (static_cast<std::size_t>(py) * static_cast<std::size_t>(frame.width) + static_cast<std::size_t>(px)) * 4ull;
        const std::uint8_t b = frame.bgra[idx + 0];
        const std::uint8_t g = frame.bgra[idx + 1];
        const std::uint8_t r = frame.bgra[idx + 2];
        if (ch == 0) return static_cast<float>(r) / 255.0f;
        if (ch == 1) return static_cast<float>(g) / 255.0f;
        return static_cast<float>(b) / 255.0f;
    };

    const float v00 = get_rgb01(x0, y0, c);
    const float v10 = get_rgb01(x1, y0, c);
    const float v01 = get_rgb01(x0, y1, c);
    const float v11 = get_rgb01(x1, y1, c);

    const float vx0 = v00 + (v10 - v00) * tx;
    const float vx1 = v01 + (v11 - v01) * tx;
    return vx0 + (vx1 - vx0) * ty;
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
    std::vector<float> input(1ull * 3ull * static_cast<std::size_t>(target_h) * static_cast<std::size_t>(target_w), 0.0f);

    // 与常见 CLIP 图像预处理对齐：
    // 1) 保持纵横比，将短边缩放到约 256（当输入为 224 时）
    // 2) 再做中心裁剪到模型输入尺寸
    // 3) 双线性采样，RGB 通道，范围 [0,1]
    const float resize_scale = 256.0f / 224.0f;
    const int resize_short = std::max(1, static_cast<int>(std::round(static_cast<float>(std::min(target_h, target_w)) * resize_scale)));

    const int src_w = frame.width;
    const int src_h = frame.height;
    const float scale = static_cast<float>(resize_short) / static_cast<float>(std::min(src_w, src_h));
    const int resized_w = std::max(1, static_cast<int>(std::round(static_cast<float>(src_w) * scale)));
    const int resized_h = std::max(1, static_cast<int>(std::round(static_cast<float>(src_h) * scale)));

    const float crop_x0 = static_cast<float>(resized_w - target_w) * 0.5f;
    const float crop_y0 = static_cast<float>(resized_h - target_h) * 0.5f;

    // 与官方 preprocess 对齐：RGB [0,1] 后再按通道做 normalize。
    constexpr float kMean[3] = {0.48145466f, 0.4578275f, 0.40821073f};
    constexpr float kStd[3] = {0.26862954f, 0.26130258f, 0.27577711f};

    const std::size_t plane = static_cast<std::size_t>(target_h) * static_cast<std::size_t>(target_w);
    for (int y = 0; y < target_h; ++y) {
        for (int x = 0; x < target_w; ++x) {
            const float rx = crop_x0 + static_cast<float>(x) + 0.5f;
            const float ry = crop_y0 + static_cast<float>(y) + 0.5f;

            // 反向映射到原图坐标（像素中心对齐）
            const float src_xf = rx / scale - 0.5f;
            const float src_yf = ry / scale - 0.5f;

            const float r = SampleBgraBilinearAsRgb01(frame, src_xf, src_yf, 0);
            const float g = SampleBgraBilinearAsRgb01(frame, src_xf, src_yf, 1);
            const float b = SampleBgraBilinearAsRgb01(frame, src_xf, src_yf, 2);

            const std::size_t o0 = static_cast<std::size_t>(y) * static_cast<std::size_t>(target_w) + static_cast<std::size_t>(x);
            input[o0] = (r - kMean[0]) / kStd[0];
            input[plane + o0] = (g - kMean[1]) / kStd[1];
            input[plane * 2 + o0] = (b - kMean[2]) / kStd[2];
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

}  // namespace k2d
