#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

namespace desktoper2D {

struct RuntimeWindowState {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Tray *tray = nullptr;
    SDL_TrayEntry *entry_click_through = nullptr;
    SDL_TrayEntry *entry_show_hide = nullptr;

    SDL_Texture *demo_texture = nullptr;
    int demo_texture_w = 0;
    int demo_texture_h = 0;

    bool click_through = false;
    bool window_visible = true;
    int window_w = 0;
    int window_h = 0;
    SDL_Rect interactive_rect{0, 0, 0, 0};
};

}  // namespace desktoper2D
