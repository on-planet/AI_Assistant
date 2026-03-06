#pragma once

#include <SDL3/SDL.h>

#include "k2d/editor/editor_input.h"

#include <functional>
#include <string>

namespace k2d {

struct AppInputControllerContext {
    bool *running = nullptr;
    bool *show_debug_stats = nullptr;
    bool *gui_enabled = nullptr;
    bool *edit_mode = nullptr;
    bool *manual_param_mode = nullptr;

    std::function<void()> toggle_edit_mode;
    std::function<void()> toggle_manual_param_mode;

    std::function<void(bool)> cycle_selected_part;

    std::function<void(float)> adjust_selected_param;
    std::function<void()> reset_selected_param;
    std::function<void()> reset_all_params;

    std::function<void()> save_model;
    std::function<void()> save_project;
    std::function<void()> save_project_as;
    std::function<void()> load_project;
    std::function<void()> undo_edit;
    std::function<void()> redo_edit;

    std::function<void(float, float, bool, Uint8)> on_mouse_button_down;
    std::function<void()> on_mouse_button_up;
    std::function<void(float, float)> on_mouse_motion;
};

void HandleNonEditKeyDown(const AppInputControllerContext &ctx, SDL_Keycode key, bool shift_pressed);
void LogUserActionFeedback(const char *action,
                           bool accepted,
                           const char *module,
                           const std::string &reason = "");
EditorInputCallbacks BuildEditorInputCallbacks(const AppInputControllerContext &ctx);
void HandleAppInputEvent(const SDL_Event &event,
                         bool edit_mode,
                         const EditorInputCallbacks &editor_callbacks,
                         const AppInputControllerContext &non_edit_ctx);

}  // namespace k2d

