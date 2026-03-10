#pragma once

#include <cstdint>
#include <string>

namespace desktoper2D {

enum class ChatProviderKind {
    Offline,
    Cloud,
    Hybrid,
};

struct ChatRequest {
    std::string user_text;
    std::string system_prompt;
    std::string language = "zh";
    int max_tokens = 256;
    float temperature = 0.7f;
};

struct ChatResponse {
    bool ok = false;
    std::string text;
    std::string error;
    std::int64_t latency_ms = 0;

    ChatProviderKind provider = ChatProviderKind::Offline;
    bool cloud_attempted = false;
    bool cloud_succeeded = false;
    bool fallback_to_offline = false;
    std::string switch_reason;
};

class IChatProvider {
public:
    virtual ~IChatProvider() = default;

    virtual bool Init(std::string *out_error) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual bool Generate(const ChatRequest &request,
                          ChatResponse &out_response,
                          std::string *out_error) = 0;

    virtual ChatProviderKind Kind() const noexcept = 0;
    virtual const char *Name() const noexcept = 0;
};

}  // namespace desktoper2D
