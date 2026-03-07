#pragma once

#include <deque>
#include <string>

#include "k2d/lifecycle/asr/asr_provider.h"

namespace k2d {

struct AsrSessionState {
    std::deque<std::string> utterances;
    std::size_t max_utterances = 12;
    std::string session_text;
};

// 纯函数式更新：可单测（输入识别结果，输出会话缓存状态）。
void UpdateAsrSessionState(const AsrRecognitionResult &result,
                           AsrSessionState &state);

}  // namespace k2d
