#include "desktoper2D/lifecycle/app_lifecycle.h"

#include <SDL3/SDL.h>

#include "desktoper2D/controllers/app_loop.h"
#include "desktoper2D/lifecycle/bridge_assembler.h"
#include "desktoper2D/lifecycle/events/app_event_handler.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/systems/runtime_tick_entry.h"
#include "desktoper2D/lifecycle/ui/runtime_render_entry.h"

namespace desktoper2D {

void AppLifecycleRun(AppLifecycleContext &ctx) {
    const AppLifecyclePhaseCapabilityContext run_ctx = ctx.RunCapability();
    AppRuntime *runtime_ptr = run_ctx.RequireRuntime();
    if (!runtime_ptr) {
        return;
    }
    AppRuntime &runtime = *runtime_ptr;
    desktoper2D::RunAppLoop(desktoper2D::AppLoopContext{
        .running = &runtime.running,
        .window_visible = &runtime.window_state.window_visible,
        .model_time = &runtime.model_time,
        .editor_status_ttl = &runtime.editor_status_ttl,
        .debug_frame_ms = &runtime.debug_frame_ms,
        .debug_fps = &runtime.debug_fps,
        .debug_fps_accum_sec = &runtime.debug_fps_accum_sec,
        .debug_fps_accum_frames = &runtime.debug_fps_accum_frames,
        .handle_event = [&runtime](const SDL_Event &event) {
            HandleAppRuntimeEvent(runtime, event, BuildAppEventHandlerBridge(runtime));
        },
        .on_update = [&runtime](float dt) {
            RunRuntimeTickEntry(runtime, dt, BuildRuntimeTickBridge(runtime));
        },
        .on_render = [&runtime]() {
            RunRuntimeRenderEntry(runtime, BuildRuntimeRenderBridge(runtime));
        },
        .on_editor_status_expired = [&runtime]() {
            runtime.editor_status.clear();
        },
    });
    run_ctx.SetExitCode(0);
}

}  // namespace desktoper2D
