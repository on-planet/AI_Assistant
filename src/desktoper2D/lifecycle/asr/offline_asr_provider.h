#pragma once

#include <string>

#include "desktoper2D/lifecycle/asr/asr_provider.h"

namespace desktoper2D {

class OfflineAsrProvider final : public IAsrProvider {
public:
    explicit OfflineAsrProvider(std::string model_path = "assets/default_plugins/asr/resources/sense-voice-encoder-int8.onnx");

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
    std::string tokenizer_path_ = "assets/default_plugins/asr/resources/chn_jpn_yue_eng_ko_spectok.bpe.model";
    std::string vad_model_path_ = "assets/default_plugins/asr/resources/fsmnvad-offline.onnx";
    std::string vad_config_path_ = "assets/default_plugins/asr/resources/fsmn-config.yaml";
    std::string am_mvn_path_ = "assets/default_plugins/asr/resources/am.mvn";
    std::string fsmn_am_mvn_path_ = "assets/default_plugins/asr/resources/fsmn-am.mvn";
    bool ready_ = false;
};

}  // namespace desktoper2D
