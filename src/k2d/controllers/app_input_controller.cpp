#include "k2d/controllers/app_input_controller.h"

namespace k2d {

void HandleNonEditKeyDown(const AppInputControllerContext &ctx, SDL_Keycode key, bool shift_pressed) {
    switch (key) {
        case SDLK_ESCAPE:
            if (ctx.running) {
                *ctx.running = false;
            }
            break;
        case SDLK_F1:
            if (ctx.gui_enabled) {
                *ctx.gui_enabled = !*ctx.gui_enabled;
            }
            break;
        case SDLK_E:
            if (ctx.toggle_edit_mode) {
                ctx.toggle_edit_mode();
            }
            break;
        case SDLK_M:
            if (ctx.toggle_manual_param_mode) {
                ctx.toggle_manual_param_mode();
            }
            break;
        case SDLK_TAB:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.cycle_selected_part) {
                ctx.cycle_selected_part(shift_pressed);
            }
            break;
        case SDLK_LEFT:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.adjust_selected_param) {
                ctx.adjust_selected_param(-0.03f);
            }
            break;
        case SDLK_RIGHT:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.adjust_selected_param) {
                ctx.adjust_selected_param(0.03f);
            }
            break;
        case SDLK_UP:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.adjust_selected_param) {
                ctx.adjust_selected_param(0.12f);
            }
            break;
        case SDLK_DOWN:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.adjust_selected_param) {
                ctx.adjust_selected_param(-0.12f);
            }
            break;
        case SDLK_SPACE:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.reset_selected_param) {
                ctx.reset_selected_param();
            }
            break;
        case SDLK_R:
            if (ctx.manual_param_mode && *ctx.manual_param_mode && ctx.reset_all_params) {
                ctx.reset_all_params();
            }
            break;
        default:
            break;
    }
}

EditorInputCallbacks BuildEditorInputCallbacks(const AppInputControllerContext &ctx) {
    return EditorInputCallbacks{
        .on_mouse_button_down = [ctx](float mx, float my, bool shift_pressed, Uint8 button) {
            if (ctx.on_mouse_button_down) {
                ctx.on_mouse_button_down(mx, my, shift_pressed, button);
            }
        },
        .on_mouse_button_up = [ctx]() {
            if (ctx.on_mouse_button_up) {
                ctx.on_mouse_button_up();
            }
        },
        .on_mouse_motion = [ctx](float mx, float my) {
            if (ctx.on_mouse_motion) {
                ctx.on_mouse_motion(mx, my);
            }
        },
        .on_key_down = [ctx](SDL_Keycode key, bool shift_pressed, bool ctrl_pressed) {
            if (key == SDLK_ESCAPE) {
                if (ctx.running) {
                    *ctx.running = false;
                }
                return;
            }
            if (key == SDLK_F1) {
                if (ctx.gui_enabled) {
                    *ctx.gui_enabled = !*ctx.gui_enabled;
                }
                return;
            }
            if (key == SDLK_E) {
                if (ctx.toggle_edit_mode) {
                    ctx.toggle_edit_mode();
                }
                return;
            }
            if (key == SDLK_S && ctrl_pressed) {
                if (ctx.save_project) {
                    ctx.save_project();
                } else if (ctx.save_model) {
                    ctx.save_model();
                }
                return;
            }
            if (key == SDLK_O && ctrl_pressed) {
                if (ctx.load_project) {
                    ctx.load_project();
                }
                return;
            }
            if (key == SDLK_Z && ctrl_pressed) {
                if (ctx.undo_edit) {
                    ctx.undo_edit();
                }
                return;
            }
            if (key == SDLK_Y && ctrl_pressed) {
                if (ctx.redo_edit) {
                    ctx.redo_edit();
                }
                return;
            }
            if (key == SDLK_M) {
                if (ctx.toggle_manual_param_mode) {
                    ctx.toggle_manual_param_mode();
                }
                return;
            }
            if (key == SDLK_TAB) {
                if (ctx.cycle_selected_part) {
                    ctx.cycle_selected_part(shift_pressed);
                }
                return;
            }
            if (ctx.manual_param_mode && *ctx.manual_param_mode) {
                if ((key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) && ctx.adjust_selected_param) {
                    const float delta =
                        key == SDLK_LEFT ? -0.03f :
                        key == SDLK_RIGHT ? 0.03f :
                        key == SDLK_UP ? 0.12f : -0.12f;
                    ctx.adjust_selected_param(delta);
                } else if (key == SDLK_SPACE && ctx.reset_selected_param) {
                    ctx.reset_selected_param();
                } else if (key == SDLK_R && ctx.reset_all_params) {
                    ctx.reset_all_params();
                }
            }
        },
    };
}

void HandleAppInputEvent(const SDL_Event &event,
                         bool edit_mode,
                         const EditorInputCallbacks &editor_callbacks,
                         const AppInputControllerContext &non_edit_ctx) {
    if (edit_mode) {
        DispatchEditorInputEvent(event, editor_callbacks);
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN) {
        HandleNonEditKeyDown(non_edit_ctx, event.key.key, (event.key.mod & SDL_KMOD_SHIFT) != 0);
    }
}

}  // namespace k2d

