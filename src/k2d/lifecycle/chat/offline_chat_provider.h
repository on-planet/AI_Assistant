#pragma once

#include <string>

#include "k2d/lifecycle/chat/chat_provider.h"

namespace k2d {

class OfflineChatProvider final : public IChatProvider {
public:
    explicit OfflineChatProvider(std::string model_path = "assets/llm/qwen2.5-1.5b-instruct.gguf");

    bool Init(std::string *out_error) override;
    void Shutdown() noexcept override;

    bool Generate(const ChatRequest &request,
                  ChatResponse &out_response,
                  std::string *out_error) override;

    ChatProviderKind Kind() const noexcept override { return ChatProviderKind::Offline; }
    const char *Name() const noexcept override { return "offline_llm"; }

private:
    std::string model_path_;
    bool ready_ = false;
};

}  // namespace k2d
