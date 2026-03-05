#include "k2d/lifecycle/chat/offline_chat_provider.h"

#include <chrono>
#include <filesystem>

namespace k2d {

OfflineChatProvider::OfflineChatProvider(std::string model_path)
    : model_path_(std::move(model_path)) {}

bool OfflineChatProvider::Init(std::string *out_error) {
    std::error_code ec;
    ready_ = std::filesystem::exists(model_path_, ec);
    if (!ready_) {
        if (out_error) {
            *out_error = "offline chat model missing";
        }
        return false;
    }
    if (out_error) {
        out_error->clear();
    }
    return true;
}

void OfflineChatProvider::Shutdown() noexcept {
    ready_ = false;
}

bool OfflineChatProvider::Generate(const ChatRequest &request,
                                   ChatResponse &out_response,
                                   std::string *out_error) {
    const auto t0 = std::chrono::steady_clock::now();

    if (!ready_) {
        const std::string err = "offline chat provider not initialized";
        out_response = ChatResponse{};
        out_response.ok = false;
        out_response.error = err;
        out_response.provider = ChatProviderKind::Offline;
        if (out_error) {
            *out_error = err;
        }
        return false;
    }

    out_response = ChatResponse{};
    out_response.ok = true;
    out_response.provider = ChatProviderKind::Offline;

    if (request.user_text.empty()) {
        out_response.text = "input_required";
    } else {
        out_response.text = "offline_placeholder: " + request.user_text;
    }

    const auto t1 = std::chrono::steady_clock::now();
    out_response.latency_ms = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    if (out_error) {
        out_error->clear();
    }
    return true;
}

}  // namespace k2d
