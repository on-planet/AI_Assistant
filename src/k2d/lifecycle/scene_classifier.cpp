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

    auto sample_bgra = [&](int x, int y, int c) -> float {
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

    for (int y = 0; y < target_h; ++y) {
        const float src_yf = (static_cast<float>(y) + 0.5f) * static_cast<float>(frame.height) / static_cast<float>(target_h) - 0.5f;
        const int sy = static_cast<int>(std::round(src_yf));
        for (int x = 0; x < target_w; ++x) {
            const float src_xf = (static_cast<float>(x) + 0.5f) * static_cast<float>(frame.width) / static_cast<float>(target_w) - 0.5f;
            const int sx = static_cast<int>(std::round(src_xf));

            const std::size_t o0 = static_cast<std::size_t>(y) * static_cast<std::size_t>(target_w) + static_cast<std::size_t>(x);
            const std::size_t plane = static_cast<std::size_t>(target_h) * static_cast<std::size_t>(target_w);
            input[o0] = sample_bgra(sx, sy, 0);
            input[plane + o0] = sample_bgra(sx, sy, 1);
            input[plane * 2 + o0] = sample_bgra(sx, sy, 2);
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

        out.logits = probs;
        out.label = impl_->labels[best];
        out.score = probs[best];
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("scene classify exception: ") + e.what();
        return false;
    }

    return true;
}

}  // namespace k2d
