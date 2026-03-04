#include "k2d/lifecycle/events/app_event_handler.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "k2d/controllers/app_input_controller.h"
#include "k2d/controllers/window_controller.h"
#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

void HandleAppRuntimeEvent(AppRuntime &runtime, const SDL_Event &event, const AppEventHandlerBridge &bridge) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        runtime.running = false;
        return;
    }

    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        runtime.window_w = event.window.data1;
        runtime.window_h = event.window.data2;
        runtime.interactive_rect = k2d::ComputeInteractiveRect(runtime.window_w, runtime.window_h);
        k2d::ApplyWindowShape(runtime.window, runtime.window_w, runtime.window_h, runtime.interactive_rect, runtime.click_through);
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        runtime.running = false;
        return;
    }

    if (!runtime.edit_mode) {
        const bool imgui_wants_mouse = ImGui::GetIO().WantCaptureMouse;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (!imgui_wants_mouse) {
                runtime.dragging_model_whole = true;
                runtime.dragging_model_last_x = event.button.x;
                runtime.dragging_model_last_y = event.button.y;
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            runtime.dragging_model_whole = false;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION && runtime.dragging_model_whole && runtime.model_loaded) {
            if (imgui_wants_mouse) {
                runtime.dragging_model_whole = false;
            } else {
                const float dx = event.motion.x - runtime.dragging_model_last_x;
                const float dy = event.motion.y - runtime.dragging_model_last_y;
                runtime.dragging_model_last_x = event.motion.x;
                runtime.dragging_model_last_y = event.motion.y;

                for (auto &part : runtime.model.parts) {
                    if (part.parent_index < 0) {
                        part.base_pos_x += dx;
                        part.base_pos_y += dy;
                    }
                }
            }
        }
    }

    const auto non_edit_ctx = k2d::AppInputControllerContext{
        .running = &runtime.running,
        .show_debug_stats = &runtime.show_debug_stats,
        .gui_enabled = &runtime.gui_enabled,
        .edit_mode = &runtime.edit_mode,
        .manual_param_mode = &runtime.manual_param_mode,
        .toggle_edit_mode = bridge.toggle_edit_mode,
        .toggle_manual_param_mode = bridge.toggle_manual_param_mode,
        .cycle_selected_part = bridge.cycle_selected_part,
        .adjust_selected_param = bridge.adjust_selected_param,
        .reset_selected_param = bridge.reset_selected_param,
        .reset_all_params = bridge.reset_all_params,
    };

    const EditorInputCallbacks callbacks = bridge.BuildEditorInputCallbacks();
    k2d::HandleAppInputEvent(event, runtime.edit_mode, callbacks, non_edit_ctx);
}

}  // namespace k2d
