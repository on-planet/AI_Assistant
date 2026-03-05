#pragma once

#include <memory>
#include <string>

#include "k2d/lifecycle/chat/chat_provider.h"

namespace k2d {

struct HybridChatConfig {
    bool prefer_cloud = true;
    bool cloud_fallback_enabled = true;
};

class HybridChatProvider final : public IChatProvider {
public:
    HybridChatProvider(std::unique_ptr<IChatProvider> offline,
                       std::unique_ptr<IChatProvider> cloud,
                       HybridChatConfig config = {});

    bool Init(std::string *out_error) override;
    void Shutdown() noexcept override;

    bool Generate(const ChatRequest &request,
                  ChatResponse &out_response,
                  std::string *out_error) override;

    ChatProviderKind Kind() const noexcept override { return ChatProviderKind::Hybrid; }
    const char *Name() const noexcept override { return "hybrid_chat_router"; }

private:
    std::unique_ptr<IChatProvider> offline_;
    std::unique_ptr<IChatProvider> cloud_;
    HybridChatConfig config_{};
    bool ready_ = false;
};

}  // namespace k2d
