#pragma once

#include <string>

#include "k2d/lifecycle/asr/asr_provider.h"

namespace k2d {

class OfflineAsrProvider final : public IAsrProvider {
public:
    explicit OfflineAsrProvider(std::string model_path = "assets/whisper/ggml-base.bin");

    bool Init(std::string *out_error) override;
    void Shutdown() noexcept override;

    bool Recognize(const AsrAudioChunk &chunk,
                   const AsrRecognitionOptions &options,
                   AsrRecognitionResult &out_result,
                   std::string *out_error) override;

    AsrProviderKind Kind() const noexcept override { return AsrProviderKind::Offline; }
    const char *Name() const noexcept override { return "offline_whispercpp"; }

private:
    std::string model_path_;
    bool ready_ = false;
};

}  // namespace k2d
