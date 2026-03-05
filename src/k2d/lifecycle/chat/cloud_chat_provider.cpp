#include "k2d/lifecycle/chat/cloud_chat_provider.h"

#include <chrono>

namespace k2d {

CloudChatProvider::CloudChatProvider(std::string endpoint, std::string api_key, std::string model_name)
    : endpoint_(std::move(endpoint)), api_key_(std::move(api_key)), model_name_(std::move(model_name)) {}

bool CloudChatProvider::Init(std::string *out_error) {
    ready_ = !endpoint_.empty() && !api_key_.empty() && !model_name_.empty();
    if (!ready_) {
        if (out_error) {
            *out_error = "cloud chat config invalid";
        }
        return false;
    }
    if (out_error) {
        out_error->clear();
    }
    return true;
}

void CloudChatProvider::Shutdown() noexcept {
    ready_ = false;
}

bool CloudChatProvider::Generate(const ChatRequest &request,
                                 ChatResponse &out_response,
                                 std::string *out_error) {
    const auto t0 = std::chrono::steady_clock::now();

    if (!ready_) {
        const std::string err = "cloud chat provider not initialized";
        out_response = ChatResponse{};
        out_response.ok = false;
        out_response.error = err;
        out_response.provider = ChatProviderKind::Cloud;
        if (out_error) {
            *out_error = err;
        }
        return false;
    }

    out_response = ChatResponse{};
    out_response.ok = true;
    out_response.provider = ChatProviderKind::Cloud;

    if (request.user_text.empty()) {
        out_response.text = "input_required";
    } else {
        out_response.text = "cloud_placeholder: " + request.user_text;
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
