#pragma once

#include <SDL3/SDL.h>

#include <functional>

namespace desktoper2D {

struct EditorInputCallbacks {
    std::function<void(float, float, bool, Uint8)> on_mouse_button_down;
    std::function<void()> on_mouse_button_up;
    std::function<void(float, float)> on_mouse_motion;
    std::function<void(SDL_Keycode, bool, bool)> on_key_down;
};

void DispatchEditorInputEvent(const SDL_Event &event, const EditorInputCallbacks &callbacks);

}  // namespace desktoper2D
