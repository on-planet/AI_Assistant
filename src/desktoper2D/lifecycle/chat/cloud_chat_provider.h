#pragma once

#include <string>

#include "desktoper2D/lifecycle/chat/chat_provider.h"

namespace desktoper2D {

class CloudChatProvider final : public IChatProvider {
public:
    CloudChatProvider(std::string endpoint, std::string api_key, std::string model_name = "gpt-4o-mini");

    bool Init(std::string *out_error) override;
    void Shutdown() noexcept override;

    bool Generate(const ChatRequest &request,
                  ChatResponse &out_response,
                  std::string *out_error) override;

    ChatProviderKind Kind() const noexcept override { return ChatProviderKind::Cloud; }
    const char *Name() const noexcept override { return "cloud_chat"; }

private:
    std::string endpoint_;
    std::string api_key_;
    std::string model_name_;
    bool ready_ = false;
};

}  // namespace desktoper2D
