#include "k2d/lifecycle/asr/offline_asr_provider.h"

#include <chrono>
#include <filesystem>

namespace k2d {

OfflineAsrProvider::OfflineAsrProvider(std::string model_path)
    : model_path_(std::move(model_path)) {}

bool OfflineAsrProvider::Init(std::string *out_error) {
    std::error_code ec;
    ready_ = std::filesystem::exists(model_path_, ec);
    if (!ready_) {
        if (out_error) {
            *out_error = "offline model not found: " + model_path_;
        }
        return false;
    }
    if (out_error) {
        out_error->clear();
    }
    return true;
}

void OfflineAsrProvider::Shutdown() noexcept {
    ready_ = false;
}

bool OfflineAsrProvider::Recognize(const AsrAudioChunk &chunk,
                                   const AsrRecognitionOptions &options,
                                   AsrRecognitionResult &out_result,
                                   std::string *out_error) {
    const auto t0 = std::chrono::steady_clock::now();

    if (!ready_) {
        const std::string err = "offline provider not initialized";
        if (out_error) {
            *out_error = err;
        }
        out_result = AsrRecognitionResult{};
        out_result.ok = false;
        out_result.error = err;
        out_result.provider = AsrProviderKind::Offline;
        return false;
    }

    out_result = AsrRecognitionResult{};
    out_result.ok = true;
    out_result.provider = AsrProviderKind::Offline;
    out_result.is_final = chunk.is_final;
    out_result.confidence = chunk.samples.empty() ? 0.2f : 0.65f;
    out_result.text = chunk.samples.empty()
                          ? ""
                          : (options.language == "zh" ? "[离线识别占位结果]" : "[offline recognition placeholder]");

    const auto t1 = std::chrono::steady_clock::now();
    out_result.latency_ms =
        static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    if (out_error) {
        out_error->clear();
    }
    return true;
}

}  // namespace k2d
