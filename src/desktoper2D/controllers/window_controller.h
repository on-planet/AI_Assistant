#pragma once

#include "desktoper2D/lifecycle/state/runtime_window_state.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

namespace desktoper2D {

SDL_Rect ComputeInteractiveRect(int w, int h);

bool CreateWindowAndRenderer(RuntimeWindowState &window_state, const char *title, int width, int height);
void DestroyWindowAndRenderer(RuntimeWindowState &window_state);

void SyncWindowSize(RuntimeWindowState &window_state);
void RefreshInteractiveRect(RuntimeWindowState &window_state);
void SetWindowOpacity(const RuntimeWindowState &window_state, float opacity);
void SetWindowHitTestCallback(const RuntimeWindowState &window_state, SDL_HitTest callback, void *callback_data);
void ApplyWindowVisibility(const RuntimeWindowState &window_state);

bool ApplyWindowShape(SDL_Window *window, int w, int h, const SDL_Rect &interactive, bool click_through);
bool ApplyWindowShape(const RuntimeWindowState &window_state);

void SetClickThrough(RuntimeWindowState &window_state, bool enabled);

SDL_Surface *CreateTrayIconSurface();

void SetDemoTexture(RuntimeWindowState &window_state, SDL_Texture *texture, int width, int height);
void DestroyDemoTexture(RuntimeWindowState &window_state);

void UpdateWindowVisibilityLabel(SDL_TrayEntry *entry_show_hide, bool window_visible);
void UpdateWindowVisibilityLabel(const RuntimeWindowState &window_state);

void ToggleWindowVisibility(SDL_Window *window, bool *window_visible);
void ToggleWindowVisibility(RuntimeWindowState &window_state);

SDL_HitTestResult WindowHitTest(bool click_through,
                                bool edit_mode,
                                const SDL_Rect &interactive_rect,
                                const SDL_Point *area);
SDL_HitTestResult WindowHitTest(const RuntimeWindowState &window_state, bool edit_mode, const SDL_Point *area);

void PresentWindowFrame(const RuntimeWindowState &window_state);

}  // namespace desktoper2D
