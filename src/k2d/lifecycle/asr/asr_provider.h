#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace k2d {

enum class AsrProviderKind {
    Offline,
    Cloud,
    Hybrid,
};

struct AsrAudioChunk {
    // 16kHz mono PCM float [-1, 1]
    std::vector<float> samples;
    int sample_rate_hz = 16000;
    bool is_final = false;
};

struct AsrRecognitionOptions {
    std::string language = "zh";
    bool enable_punctuation = true;
    std::optional<std::string> hint;
};

struct AsrRecognitionResult {
    bool ok = false;
    std::string text;
    float confidence = 0.0f;
    bool is_final = false;
    std::string error;
    std::int64_t latency_ms = 0;
    AsrProviderKind provider = AsrProviderKind::Offline;
};

class IAsrProvider {
public:
    virtual ~IAsrProvider() = default;

    virtual bool Init(std::string *out_error) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool Recognize(const AsrAudioChunk &chunk,
                           const AsrRecognitionOptions &options,
                           AsrRecognitionResult &out_result,
                           std::string *out_error) = 0;

    virtual AsrProviderKind Kind() const noexcept = 0;
    virtual const char *Name() const noexcept = 0;
};

}  // namespace k2d
