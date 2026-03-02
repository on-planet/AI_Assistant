#include "k2d/lifecycle/asr/cloud_asr_provider.h"

#include <chrono>

namespace k2d {

CloudAsrProvider::CloudAsrProvider(std::string endpoint, std::string api_key)
    : endpoint_(std::move(endpoint)), api_key_(std::move(api_key)) {}

bool CloudAsrProvider::Init(std::string *out_error) {
    ready_ = !endpoint_.empty() && !api_key_.empty();
    if (!ready_) {
        if (out_error) {
            *out_error = "cloud provider config invalid: endpoint/api_key required";
        }
        return false;
    }
    if (out_error) {
        out_error->clear();
    }
    return true;
}

void CloudAsrProvider::Shutdown() noexcept {
    ready_ = false;
}

bool CloudAsrProvider::Recognize(const AsrAudioChunk &chunk,
                                 const AsrRecognitionOptions &options,
                                 AsrRecognitionResult &out_result,
                                 std::string *out_error) {
    const auto t0 = std::chrono::steady_clock::now();

    if (!ready_) {
        const std::string err = "cloud provider not initialized";
        if (out_error) {
            *out_error = err;
        }
        out_result = AsrRecognitionResult{};
        out_result.ok = false;
        out_result.error = err;
        out_result.provider = AsrProviderKind::Cloud;
        return false;
    }

    out_result = AsrRecognitionResult{};
    out_result.ok = true;
    out_result.provider = AsrProviderKind::Cloud;
    out_result.is_final = chunk.is_final;
    out_result.confidence = chunk.samples.empty() ? 0.1f : 0.78f;
    out_result.text = chunk.samples.empty()
                          ? ""
                          : (options.language == "zh" ? "[云端识别占位结果]" : "[cloud recognition placeholder]");

    const auto t1 = std::chrono::steady_clock::now();
    out_result.latency_ms =
        static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    if (out_error) {
        out_error->clear();
    }
    return true;
}

}  // namespace k2d
