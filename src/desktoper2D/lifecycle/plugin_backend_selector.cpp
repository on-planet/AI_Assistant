#include "desktoper2D/lifecycle/plugin_backend_selector.h"

#include <algorithm>

#include "desktoper2D/core/async_logger.h"

namespace desktoper2D {

std::vector<std::string> NormalizeBackendPriority(const JsonValue *arr) {
    std::vector<std::string> out;
    if (!arr || !arr->isArray() || !arr->asArray()) {
        return {"cpu"};
    }
    for (const auto &v : *arr->asArray()) {
        if (!v.isString() || !v.asString()) continue;
        std::string b = *v.asString();
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (b == "dml") b = "directml";
        if (b == "cpu" || b == "cuda" || b == "directml") {
            if (std::find(out.begin(), out.end(), b) == out.end()) {
                out.push_back(b);
            }
        }
    }
    if (out.empty()) out.push_back("cpu");
    return out;
}

bool ApplyBackendPriorityToSessionOptions(const std::vector<std::string> &priority,
                                          Ort::SessionOptions &opts,
                                          std::string *out_backend,
                                          std::string *out_error) {
    if (out_backend) *out_backend = "cpu";
    if (out_error) out_error->clear();

    for (const auto &b : priority) {
        if (b == "cuda") {
#if defined(K2D_HAS_ORT_CUDA_FACTORY)
            OrtCUDAProviderOptions cuda_opts{};
            OrtStatus *st = OrtSessionOptionsAppendExecutionProvider_CUDA(opts, &cuda_opts);
            if (st == nullptr) {
                if (out_backend) *out_backend = "cuda";
                if (out_error) out_error->clear();
                LogInfo("plugin backend selected: cuda");
                return true;
            }
            if (out_error) *out_error = "append cuda provider failed";
            LogError("plugin backend cuda append failed, fallback to next");
#else
            if (out_error) *out_error = "cuda provider not available in this build";
            LogInfo("plugin backend cuda not available in this build");
#endif
            continue;
        }

        if (b == "directml") {
#if defined(K2D_HAS_ORT_DML_FACTORY)
            OrtStatus *st = OrtSessionOptionsAppendExecutionProvider_DML(opts, 0);
            if (st == nullptr) {
                if (out_backend) *out_backend = "directml";
                if (out_error) out_error->clear();
                LogInfo("plugin backend selected: directml");
                return true;
            }
            if (out_error) *out_error = "append directml provider failed";
            LogError("plugin backend directml append failed, fallback to next");
#else
            if (out_error) *out_error = "directml provider not available in this build";
            LogInfo("plugin backend directml not available in this build");
#endif
            continue;
        }

        if (b == "cpu") {
            if (out_backend) *out_backend = "cpu";
            if (out_error) out_error->clear();
            LogInfo("plugin backend selected: cpu");
            return true;
        }
    }

    if (out_backend) *out_backend = "cpu";
    if (out_error) out_error->clear();
    LogInfo("plugin backend defaulted to cpu");
    return true;
}

}  // namespace desktoper2D
