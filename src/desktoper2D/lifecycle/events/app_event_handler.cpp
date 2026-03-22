#include "desktoper2D/lifecycle/events/app_event_handler.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "desktoper2D/controllers/app_input_controller.h"
#include "desktoper2D/controllers/window_controller.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void HandleAppRuntimeEvent(AppRuntime &runtime, const SDL_Event &event, const AppEventHandlerBridge &bridge) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        runtime.running = false;
        return;
    }

    if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        runtime.window_state.window_w = event.window.data1;
        runtime.window_state.window_h = event.window.data2;
        desktoper2D::RefreshInteractiveRect(runtime.window_state);
        desktoper2D::ApplyWindowShape(runtime.window_state);
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        runtime.running = false;
        return;
    }

    if (!runtime.edit_mode) {
        const bool imgui_wants_mouse = ImGui::GetIO().WantCaptureMouse;

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            // 点击反应不依赖 ImGui 捕获，避免运行态点击模型时被 UI 捕获状态吞掉。
            bridge.OnHeadPatMouseDown(event.button.x, event.button.y);
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

    const auto non_edit_ctx = desktoper2D::AppInputControllerContext{
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
    desktoper2D::HandleAppInputEvent(event, runtime.edit_mode, callbacks, non_edit_ctx);
}

}  // namespace desktoper2D
