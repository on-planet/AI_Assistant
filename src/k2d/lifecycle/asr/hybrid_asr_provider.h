#pragma once

#include <memory>
#include <string>

#include "k2d/lifecycle/asr/asr_provider.h"

namespace k2d {

struct HybridAsrConfig {
    float offline_confidence_threshold = 0.55f;
    bool cloud_fallback_enabled = true;
};

class HybridAsrProvider final : public IAsrProvider {
public:
    HybridAsrProvider(std::unique_ptr<IAsrProvider> offline,
                      std::unique_ptr<IAsrProvider> cloud,
                      HybridAsrConfig config = {});

    bool Init(std::string *out_error) override;
    void Shutdown() noexcept override;

    bool Recognize(const AsrAudioChunk &chunk,
                   const AsrRecognitionOptions &options,
                   AsrRecognitionResult &out_result,
                   std::string *out_error) override;

    AsrProviderKind Kind() const noexcept override { return AsrProviderKind::Hybrid; }
    const char *Name() const noexcept override { return "hybrid_asr_router"; }

private:
    std::unique_ptr<IAsrProvider> offline_;
    std::unique_ptr<IAsrProvider> cloud_;
    HybridAsrConfig config_{};
    bool ready_ = false;
};

}  // namespace k2d
