#pragma once

#include <string>

#include "k2d/lifecycle/asr/asr_provider.h"

namespace k2d {

class CloudAsrProvider final : public IAsrProvider {
public:
    CloudAsrProvider(std::string endpoint, std::string api_key);

    bool Init(std::string *out_error) override;
    void Shutdown() noexcept override;

    bool Recognize(const AsrAudioChunk &chunk,
                   const AsrRecognitionOptions &options,
                   AsrRecognitionResult &out_result,
                   std::string *out_error) override;

    AsrProviderKind Kind() const noexcept override { return AsrProviderKind::Cloud; }
    const char *Name() const noexcept override { return "cloud_asr"; }

private:
    std::string endpoint_;
    std::string api_key_;
    bool ready_ = false;
};

}  // namespace k2d
