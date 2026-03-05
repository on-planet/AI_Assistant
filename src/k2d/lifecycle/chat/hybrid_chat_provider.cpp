#include "k2d/lifecycle/chat/hybrid_chat_provider.h"

namespace k2d {

HybridChatProvider::HybridChatProvider(std::unique_ptr<IChatProvider> offline,
                                       std::unique_ptr<IChatProvider> cloud,
                                       HybridChatConfig config)
    : offline_(std::move(offline)), cloud_(std::move(cloud)), config_(config) {}

bool HybridChatProvider::Init(std::string *out_error) {
    if (!offline_) {
        if (out_error) {
            *out_error = "hybrid chat requires offline provider";
        }
        return false;
    }

    std::string offline_err;
    const bool offline_ok = offline_->Init(&offline_err);
    if (!offline_ok) {
        if (out_error) {
            *out_error = offline_err.empty() ? "offline chat init failed" : offline_err;
        }
        return false;
    }

    if (cloud_) {
        std::string cloud_err;
        const bool cloud_ok = cloud_->Init(&cloud_err);
        if (!cloud_ok && !config_.cloud_fallback_enabled) {
            if (out_error) {
                *out_error = cloud_err.empty() ? "cloud chat init failed" : cloud_err;
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

void HybridChatProvider::Shutdown() noexcept {
    if (offline_) {
        offline_->Shutdown();
    }
    if (cloud_) {
        cloud_->Shutdown();
    }
    ready_ = false;
}

bool HybridChatProvider::Generate(const ChatRequest &request,
                                  ChatResponse &out_response,
                                  std::string *out_error) {
    if (!ready_ || !offline_) {
        const std::string err = "hybrid chat provider not initialized";
        out_response = ChatResponse{};
        out_response.ok = false;
        out_response.error = err;
        out_response.provider = ChatProviderKind::Hybrid;
        out_response.switch_reason = "strategy_not_ready";
        if (out_error) {
            *out_error = err;
        }
        return false;
    }

    auto use_offline = [&]() {
        std::string off_err;
        ChatResponse off{};
        const bool ok = offline_->Generate(request, off, &off_err);
        out_response = std::move(off);
        out_response.switch_reason = "offline_only";
        if (!ok && out_error) {
            *out_error = off_err;
        } else if (out_error) {
            out_error->clear();
        }
        return ok;
    };

    if (!cloud_ || !config_.prefer_cloud) {
        return use_offline();
    }

    ChatResponse cloud_rsp{};
    std::string cloud_err;
    const bool cloud_ok = cloud_->Generate(request, cloud_rsp, &cloud_err);
    if (cloud_ok && cloud_rsp.ok) {
        out_response = std::move(cloud_rsp);
        out_response.cloud_attempted = true;
        out_response.cloud_succeeded = true;
        out_response.switch_reason = "prefer_cloud";
        if (out_error) {
            out_error->clear();
        }
        return true;
    }

    const bool offline_ok = use_offline();
    out_response.cloud_attempted = true;
    out_response.cloud_succeeded = false;
    out_response.fallback_to_offline = true;
    out_response.switch_reason = "cloud_error_fallback_offline";
    if (!offline_ok && out_response.error.empty()) {
        out_response.error = cloud_err;
    }
    if (out_error && !offline_ok && !cloud_err.empty()) {
        *out_error = cloud_err;
    }
    return offline_ok;
}

}  // namespace k2d
