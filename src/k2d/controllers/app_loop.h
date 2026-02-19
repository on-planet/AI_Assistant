#pragma once

#include <SDL3/SDL.h>

#include <functional>

namespace k2d {

struct AppLoopContext {
    bool *running = nullptr;
    bool *window_visible = nullptr;

    float *model_time = nullptr;
    float *editor_status_ttl = nullptr;
    float *debug_frame_ms = nullptr;
    float *debug_fps = nullptr;
    float *debug_fps_accum_sec = nullptr;
    int *debug_fps_accum_frames = nullptr;

    std::function<void(const SDL_Event &)> handle_event;
    std::function<void(float)> on_update;
    std::function<void()> on_render;
    std::function<void()> on_editor_status_expired;
};

void RunAppLoop(const AppLoopContext &ctx);

}  // namespace k2d
