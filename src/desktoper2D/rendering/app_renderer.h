#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/editor/editor_gizmo.h"
#include "desktoper2D/core/model.h"

#include <functional>

namespace desktoper2D {

struct AppRenderContext {
    SDL_Renderer *renderer = nullptr;

    bool model_loaded = false;
    ModelRuntime *model = nullptr;

    SDL_Texture *demo_texture = nullptr;
    int demo_texture_w = 0;
    int demo_texture_h = 0;

    bool show_debug_stats = true;
    bool manual_param_mode = false;
    int selected_param_index = 0;

    bool edit_mode = false;
    int selected_part_index = -1;
    GizmoHandle gizmo_hover_handle = GizmoHandle::None;
    GizmoHandle gizmo_active_handle = GizmoHandle::None;

    const char *editor_status = nullptr;
    float editor_status_ttl = 0.0f;
    int window_h = 0;

    // 编辑器视图（用于画布平移/缩放）。
    float view_pan_x = 0.0f;
    float view_pan_y = 0.0f;
    float view_zoom = 1.0f;

    float debug_fps = 0.0f;
    float debug_frame_ms = 0.0f;

    std::function<bool()> has_model_parts;
    std::function<bool()> has_model_params;
    std::function<void()> ensure_selected_part_index_valid;
    std::function<void()> ensure_selected_param_index_valid;
    std::function<bool(const ModelPart &, SDL_FRect *)> compute_part_aabb;
};

void RenderAppFrame(const AppRenderContext &ctx);

}  // namespace desktoper2D

