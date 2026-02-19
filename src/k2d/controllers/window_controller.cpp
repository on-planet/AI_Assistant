#include "k2d/controllers/window_controller.h"

namespace k2d {

SDL_Rect ComputeInteractiveRect(int w, int h) {
    const int padding = 24;
    int box_w = 260;
    int box_h = 96;

    int max_w = w - padding * 2;
    int max_h = h - padding * 2;

    if (box_w > max_w) {
        box_w = max_w;
    }
    if (box_h > max_h) {
        box_h = max_h;
    }

    if (box_w < 80) {
        box_w = 80;
    }
    if (box_h < 60) {
        box_h = 60;
    }

    SDL_Rect rect{};
    rect.w = box_w;
    rect.h = box_h;
    rect.x = w - box_w - padding;
    rect.y = h - box_h - padding;

    if (rect.x < padding) {
        rect.x = padding;
    }
    if (rect.y < padding) {
        rect.y = padding;
    }

    return rect;
}

bool ApplyWindowShape(SDL_Window *window, int w, int h, const SDL_Rect &interactive, bool click_through) {
    if (w <= 0 || h <= 0) {
        return false;
    }

    SDL_Surface *shape = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!shape) {
        SDL_Log("SDL_CreateSurface failed: %s", SDL_GetError());
        return false;
    }

    Uint32 transparent = SDL_MapSurfaceRGBA(shape, 0, 0, 0, 0);
    Uint32 solid = SDL_MapSurfaceRGBA(shape, 0, 0, 0, 255);

    SDL_FillSurfaceRect(shape, nullptr, transparent);

    if (click_through) {
        SDL_FillSurfaceRect(shape, &interactive, solid);
    } else {
        SDL_FillSurfaceRect(shape, nullptr, solid);
    }

    const bool ok = SDL_SetWindowShape(window, shape);
    SDL_DestroySurface(shape);

    if (!ok) {
        SDL_Log("SDL_SetWindowShape failed: %s", SDL_GetError());
    }

    return ok;
}

SDL_Surface *CreateTrayIconSurface() {
    const int size = 24;
    SDL_Surface *surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        SDL_Log("SDL_CreateSurface failed: %s", SDL_GetError());
        return nullptr;
    }

    Uint32 base = SDL_MapSurfaceRGBA(surface, 40, 160, 220, 255);
    Uint32 accent = SDL_MapSurfaceRGBA(surface, 255, 255, 255, 255);

    SDL_FillSurfaceRect(surface, nullptr, base);

    SDL_Rect inner{6, 6, 12, 12};
    SDL_FillSurfaceRect(surface, &inner, accent);

    return surface;
}

void UpdateWindowVisibilityLabel(SDL_TrayEntry *entry_show_hide, bool window_visible) {
    if (!entry_show_hide) {
        return;
    }

    const char *label = window_visible ? "Hide Window" : "Show Window";
    SDL_SetTrayEntryLabel(entry_show_hide, label);
}

void ToggleWindowVisibility(SDL_Window *window, bool *window_visible) {
    if (!window || !window_visible) {
        return;
    }

    *window_visible = !*window_visible;

    if (*window_visible) {
        SDL_ShowWindow(window);
    } else {
        SDL_HideWindow(window);
    }
}

SDL_HitTestResult WindowHitTest(bool click_through, const SDL_Rect &interactive_rect, const SDL_Point *area) {
    if (!area) {
        return SDL_HITTEST_DRAGGABLE;
    }

    if (click_through) {
        const bool inside = area->x >= interactive_rect.x && area->x < (interactive_rect.x + interactive_rect.w) &&
                            area->y >= interactive_rect.y && area->y < (interactive_rect.y + interactive_rect.h);
        return inside ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
    }

    return SDL_HITTEST_DRAGGABLE;
}

}  // namespace k2d

