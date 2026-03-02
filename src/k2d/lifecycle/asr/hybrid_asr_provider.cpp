#include "k2d/lifecycle/asr/hybrid_asr_provider.h"

namespace k2d {

HybridAsrProvider::HybridAsrProvider(std::unique_ptr<IAsrProvider> offline,
                                     std::unique_ptr<IAsrProvider> cloud,
                                     HybridAsrConfig config)
    : offline_(std::move(offline)), cloud_(std::move(cloud)), config_(config) {}

bool HybridAsrProvider::Init(std::string *out_error) {
    if (!offline_) {
        if (out_error) {
            *out_error = "hybrid provider requires offline provider";
        }
        return false;
    }

    std::string offline_err;
    const bool offline_ok = offline_->Init(&offline_err);
    if (!offline_ok) {
        if (out_error) {
            *out_error = offline_err.empty() ? "offline init failed" : offline_err;
        }
        return false;
    }

    if (cloud_) {
        std::string cloud_err;
        const bool cloud_ok = cloud_->Init(&cloud_err);
        if (!cloud_ok && !config_.cloud_fallback_enabled) {
            if (out_error) {
                *out_error = cloud_err.empty() ? "cloud init failed" : cloud_err;
            }
            return false;
        }
    }

    ready_ = true;
    if (out_error) {
        out_error->clear();
    }
    return true;
}

void HybridAsrProvider::Shutdown() noexcept {
    if (offline_) {
        offline_->Shutdown();
    }
    if (cloud_) {
        cloud_->Shutdown();
    }
    ready_ = false;
}

bool HybridAsrProvider::Recognize(const AsrAudioChunk &chunk,
                                  const AsrRecognitionOptions &options,
                                  AsrRecognitionResult &out_result,
                                  std::string *out_error) {
    if (!ready_ || !offline_) {
        const std::string err = "hybrid provider not initialized";
        out_result = AsrRecognitionResult{};
        out_result.ok = false;
        out_result.error = err;
        out_result.provider = AsrProviderKind::Hybrid;
        if (out_error) {
            *out_error = err;
        }
        return false;
    }

    // 统一音频标准：16kHz / mono / float PCM [-1, 1]
    AsrAudioChunk normalized = chunk;
    normalized.sample_rate_hz = 16000;

    AsrRecognitionResult offline_result;
    std::string offline_err;
    const bool offline_ok = offline_->Recognize(normalized, options, offline_result, &offline_err);

    const bool should_fallback_to_cloud =
        cloud_ && config_.cloud_fallback_enabled &&
        (!offline_ok || !offline_result.ok || offline_result.confidence < config_.offline_confidence_threshold);

    if (!should_fallback_to_cloud) {
        out_result = offline_result;
        if (out_error) {
            out_error->clear();
        }
        return out_result.ok;
    }

    AsrRecognitionResult cloud_result;
    std::string cloud_err;
    const bool cloud_ok = cloud_->Recognize(normalized, options, cloud_result, &cloud_err);
    if (cloud_ok && cloud_result.ok) {
        out_result = cloud_result;
        if (out_error) {
            out_error->clear();
        }
        return true;
    }

    // 云端失败自动回退离线，保证可用性
    out_result = offline_result;
    if (!offline_result.ok && !cloud_err.empty()) {
        out_result.error = cloud_err;
    } else {
        out_result.error = cloud_err.empty() ? offline_err : ("cloud failed fallback offline: " + cloud_err);
    }
    if (out_error) {
        *out_error = out_result.error;
    }
    return out_result.ok;
}

}  // namespace k2d
