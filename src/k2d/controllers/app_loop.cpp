#include "k2d/controllers/app_loop.h"

#include <algorithm>

namespace k2d {

void RunAppLoop(const AppLoopContext &ctx) {
    if (!ctx.running || !ctx.window_visible || !ctx.model_time || !ctx.editor_status_ttl ||
        !ctx.debug_frame_ms || !ctx.debug_fps || !ctx.debug_fps_accum_sec || !ctx.debug_fps_accum_frames) {
        return;
    }

    SDL_Event event;
    Uint64 last_ticks_ms = SDL_GetTicks();
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();

    while (*ctx.running) {
        const Uint64 frame_begin = SDL_GetPerformanceCounter();

        while (SDL_PollEvent(&event)) {
            if (ctx.handle_event) {
                ctx.handle_event(event);
            }
        }

        const Uint64 now_ticks_ms = SDL_GetTicks();
        float dt = static_cast<float>(now_ticks_ms - last_ticks_ms) * 0.001f;
        last_ticks_ms = now_ticks_ms;
        dt = std::clamp(dt, 0.0f, 0.05f);
        *ctx.model_time += dt;

        if (*ctx.editor_status_ttl > 0.0f) {
            *ctx.editor_status_ttl = std::max(0.0f, *ctx.editor_status_ttl - dt);
            if (*ctx.editor_status_ttl <= 0.0f && ctx.on_editor_status_expired) {
                ctx.on_editor_status_expired();
            }
        }

        if (ctx.on_update) {
            ctx.on_update(dt);
        }

        if (*ctx.window_visible && ctx.on_render) {
            ctx.on_render();
        }

        const Uint64 frame_end = SDL_GetPerformanceCounter();
        const double frame_ms = static_cast<double>(frame_end - frame_begin) * 1000.0 /
                                static_cast<double>(perf_freq > 0 ? perf_freq : 1);
        *ctx.debug_frame_ms = static_cast<float>(frame_ms);

        *ctx.debug_fps_accum_sec += dt;
        *ctx.debug_fps_accum_frames += 1;
        if (*ctx.debug_fps_accum_sec >= 0.5f) {
            *ctx.debug_fps = static_cast<float>(*ctx.debug_fps_accum_frames) / *ctx.debug_fps_accum_sec;
            *ctx.debug_fps_accum_sec = 0.0f;
            *ctx.debug_fps_accum_frames = 0;
        }

        SDL_Delay(16);
    }
}

}  // namespace k2d

