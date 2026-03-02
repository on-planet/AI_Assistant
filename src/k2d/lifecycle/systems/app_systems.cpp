#include "k2d/lifecycle/systems/app_systems.h"

#include <algorithm>
#include <ctime>
#include <string>

#include <SDL3/SDL_log.h>

#include "k2d/lifecycle/state/app_runtime_state.h"

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
}

}  // namespace k2d
