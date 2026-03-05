#pragma once

#include <functional>

#include <SDL3/SDL.h>

#include "k2d/editor/editor_input.h"

namespace k2d {

struct AppRuntime;
struct EditorInputCallbacks;

struct AppEventHandlerBridge {
    using BuildEditorInputCallbacksFn = std::function<EditorInputCallbacks()>;
    using VoidFn = std::function<void()>;
    using BoolArgFn = std::function<void(bool)>;
    using FloatArgFn = std::function<void(float)>;
    using MouseFn = std::function<void(float, float)>;

    BuildEditorInputCallbacksFn build_editor_input_callbacks;
    VoidFn toggle_edit_mode;
    VoidFn toggle_manual_param_mode;
    BoolArgFn cycle_selected_part;
    FloatArgFn adjust_selected_param;
    VoidFn reset_selected_param;
    VoidFn reset_all_params;
    MouseFn on_head_pat_mouse_motion;
    MouseFn on_head_pat_mouse_down;

    EditorInputCallbacks BuildEditorInputCallbacks() const {
        return build_editor_input_callbacks ? build_editor_input_callbacks() : EditorInputCallbacks{};
    }

    void OnHeadPatMouseMotion(float mx, float my) const {
        if (on_head_pat_mouse_motion) {
            on_head_pat_mouse_motion(mx, my);
        }
    }

    void OnHeadPatMouseDown(float mx, float my) const {
        if (on_head_pat_mouse_down) {
            on_head_pat_mouse_down(mx, my);
        }
    }
};

void HandleAppRuntimeEvent(AppRuntime &runtime, const SDL_Event &event, const AppEventHandlerBridge &bridge);

}  // namespace k2d
