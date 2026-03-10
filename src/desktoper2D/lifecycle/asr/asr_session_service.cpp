#include "desktoper2D/lifecycle/asr/asr_session_service.h"

namespace desktoper2D {

void UpdateAsrSessionState(const AsrRecognitionResult &result,
                           AsrSessionState &state) {
    if (!result.ok || !result.is_final || result.text.empty()) {
        return;
    }

    if (state.utterances.size() >= state.max_utterances) {
        state.utterances.pop_front();
    }
    state.utterances.push_back(result.text);

    state.session_text.clear();
    for (std::size_t i = 0; i < state.utterances.size(); ++i) {
        if (i > 0) state.session_text += "\n";
        state.session_text += state.utterances[i];
    }
}

}  // namespace desktoper2D
