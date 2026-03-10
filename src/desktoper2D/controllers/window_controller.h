#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

namespace desktoper2D {

SDL_Rect ComputeInteractiveRect(int w, int h);

bool ApplyWindowShape(SDL_Window *window, int w, int h, const SDL_Rect &interactive, bool click_through);

SDL_Surface *CreateTrayIconSurface();

void UpdateWindowVisibilityLabel(SDL_TrayEntry *entry_show_hide, bool window_visible);

void ToggleWindowVisibility(SDL_Window *window, bool *window_visible);

SDL_HitTestResult WindowHitTest(bool click_through,
                                bool edit_mode,
                                const SDL_Rect &interactive_rect,
                                const SDL_Point *area);

}  // namespace desktoper2D
