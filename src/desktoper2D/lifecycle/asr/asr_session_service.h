#pragma once

#include <deque>
#include <string>

#include "desktoper2D/lifecycle/asr/asr_provider.h"
#include "desktoper2D/lifecycle/perception_pipeline.h"

namespace desktoper2D {

struct AsrSessionState {
    std::deque<std::string> utterances;
    std::size_t max_utterances = 12;
    std::string session_text;
};

// 纯函数式更新：可单测（输入识别结果，输出会话缓存状态）。
void UpdateAsrSessionState(const AsrRecognitionResult &result,
                           AsrSessionState &state);

// 将 ASR 会话态发布到感知黑板，供任务决策链路统一消费。
void PublishAsrSessionToPerception(const AsrSessionState &session_state,
                                   PerceptionPipelineState &perception_state);

}  // namespace desktoper2D
