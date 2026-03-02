#include "k2d/lifecycle/systems/app_systems.h"

#include <algorithm>
#include <ctime>
#include <string>

#include <SDL3/SDL_log.h>

#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/asr/asr_provider.h"

namespace k2d {

void TickAppSystems(AppRuntime &runtime, float dt) {
    // 迁移批次 #1：Reminder + Perception
    if (runtime.reminder_ready) {
        runtime.reminder_poll_accum_sec += std::max(0.0f, dt);
        if (runtime.reminder_poll_accum_sec >= 1.0f) {
            runtime.reminder_poll_accum_sec = 0.0f;
            const std::int64_t now_sec = static_cast<std::int64_t>(std::time(nullptr));

            std::string due_err;
            auto due_items = runtime.reminder_service.PollDueAndMarkNotified(now_sec, 8, &due_err);
            if (!due_err.empty()) {
                runtime.reminder_last_error = due_err;
            }
            for (const auto &item : due_items) {
                SDL_Log("[Reminder] due id=%lld title=%s", static_cast<long long>(item.id), item.title.c_str());
            }

            std::string list_err;
            runtime.reminder_upcoming = runtime.reminder_service.ListUpcoming(now_sec, 8, &list_err);
            if (!list_err.empty()) {
                runtime.reminder_last_error = list_err;
            }
        }
    }

    runtime.perception_pipeline.Tick(dt, runtime.perception_state);

    if (runtime.asr_ready && runtime.asr_provider) {
        runtime.asr_poll_accum_sec += std::max(0.0f, dt);
        if (runtime.asr_poll_accum_sec >= 2.0f) {
            runtime.asr_poll_accum_sec = 0.0f;

            AsrAudioChunk chunk{};
            chunk.sample_rate_hz = 16000;
            chunk.is_final = true;
            chunk.samples.assign(16000, 0.0f);

            AsrRecognitionOptions options{};
            options.language = "zh";

            AsrRecognitionResult result{};
            std::string asr_err;
            const bool ok = runtime.asr_provider->Recognize(chunk, options, result, &asr_err);
            if (ok) {
                runtime.asr_last_result = std::move(result);
                runtime.asr_last_error.clear();
            } else {
                runtime.asr_last_error = asr_err;
                SDL_Log("ASR recognize failed: %s", asr_err.c_str());
            }
        }
    }
}

}  // namespace k2d
