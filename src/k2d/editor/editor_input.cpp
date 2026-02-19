#include "k2d/editor/editor_input.h"

namespace k2d {

void DispatchEditorInputEvent(const SDL_Event &event, const EditorInputCallbacks &callbacks) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (callbacks.on_mouse_button_down) {
                const bool shift_pressed = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                callbacks.on_mouse_button_down(event.button.x, event.button.y, shift_pressed, event.button.button);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (callbacks.on_mouse_button_up) {
                callbacks.on_mouse_button_up();
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (callbacks.on_mouse_motion) {
                callbacks.on_mouse_motion(event.motion.x, event.motion.y);
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (callbacks.on_key_down) {
                const bool shift_pressed = (event.key.mod & SDL_KMOD_SHIFT) != 0;
                const bool ctrl_pressed = (event.key.mod & SDL_KMOD_CTRL) != 0;
                callbacks.on_key_down(event.key.key, shift_pressed, ctrl_pressed);
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace k2d

