#include "k2d/rendering/app_renderer.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace k2d {

namespace {

void RenderPartEditorOverlay(const AppRenderContext &ctx) {
    if (!ctx.renderer || !ctx.model || !ctx.has_model_parts || !ctx.has_model_parts()) {
        return;
    }

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);

    if (ctx.ensure_selected_part_index_valid) {
        ctx.ensure_selected_part_index_valid();
    }

    if (ctx.selected_part_index < 0 ||
        ctx.selected_part_index >= static_cast<int>(ctx.model->parts.size())) {
        return;
    }

    const ModelPart &part = ctx.model->parts[static_cast<std::size_t>(ctx.selected_part_index)];

    SDL_FRect box{};
    if (ctx.compute_part_aabb && ctx.compute_part_aabb(part, &box)) {
        const SDL_FRect glow_box{
            box.x - 3.0f,
            box.y - 3.0f,
            box.w + 6.0f,
            box.h + 6.0f,
        };
        SDL_SetRenderDrawColor(ctx.renderer, 255, 220, 64, ctx.edit_mode ? 230 : 180);
        SDL_RenderRect(ctx.renderer, &glow_box);
        SDL_SetRenderDrawColor(ctx.renderer, 255, 220, 64, 255);
        SDL_RenderRect(ctx.renderer, &box);
    }

    const float px = part.runtime_pos_x;
    const float py = part.runtime_pos_y;

    SDL_SetRenderDrawColor(ctx.renderer, 255, 64, 64, ctx.edit_mode ? 240 : 180);
    SDL_RenderLine(ctx.renderer, px - 10.0f, py, px + 10.0f, py);
    SDL_RenderLine(ctx.renderer, px, py - 10.0f, px, py + 10.0f);

    char part_line[256]{};
    std::snprintf(part_line, sizeof(part_line),
                  "%s part[%d/%zu]: %s pos(%.1f,%.1f) pivot(%.1f,%.1f)",
                  ctx.edit_mode ? "EDIT" : "SELECT",
                  ctx.selected_part_index + 1,
                  ctx.model->parts.size(),
                  part.id.c_str(),
                  part.base_pos_x,
                  part.base_pos_y,
                  part.pivot_x,
                  part.pivot_y);
    SDL_RenderDebugText(ctx.renderer, 12.0f, 108.0f, part_line);

    if (ctx.edit_mode) {
        RenderGizmoOverlay(ctx.renderer, part, ctx.gizmo_hover_handle, ctx.gizmo_active_handle);
        SDL_RenderDebugText(ctx.renderer,
                            12.0f,
                            124.0f,
                            "EditKeys: E toggle | LMB Gizmo(X/Y/Rotate/Scale) | Shift+LMB pivot | Tab prev/next | Ctrl+S save project | Ctrl+O load project");
    }
}

void RenderEditorStatus(const AppRenderContext &ctx) {
    if (!ctx.renderer || !ctx.editor_status || ctx.editor_status_ttl <= 0.0f) {
        return;
    }

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);
    SDL_RenderDebugText(ctx.renderer, 12.0f, static_cast<float>(ctx.window_h - 24), ctx.editor_status);
}

void RenderDebugStats(const AppRenderContext &ctx) {
    if (!ctx.renderer || !ctx.model) {
        return;
    }

    SDL_SetRenderDrawColor(ctx.renderer, 255, 255, 255, 255);

    char line1[128]{};
    char line2[128]{};
    char line3[128]{};
    char line4[160]{};
    char line5[256]{};
    char line6[256]{};

    std::snprintf(line1, sizeof(line1), "FPS: %.1f  Frame: %.2f ms", ctx.debug_fps, ctx.debug_frame_ms);
    std::snprintf(line2, sizeof(line2), "Parts: %d/%d", ctx.model->debug_stats.drawn_part_count,
                  ctx.model->debug_stats.part_count);
    std::snprintf(line3, sizeof(line3), "Verts: %d  Tris: %d", ctx.model->debug_stats.vertex_count,
                  ctx.model->debug_stats.triangle_count);

    SDL_RenderDebugText(ctx.renderer, 12.0f, 12.0f, line1);
    SDL_RenderDebugText(ctx.renderer, 12.0f, 28.0f, line2);
    SDL_RenderDebugText(ctx.renderer, 12.0f, 44.0f, line3);

    if (ctx.model_loaded) {
        std::snprintf(line4, sizeof(line4), "Animation Channels: %s | Manual Param: %s",
                      ctx.model->animation_channels_enabled ? "ON" : "OFF",
                      ctx.manual_param_mode ? "ON" : "OFF");
        SDL_RenderDebugText(ctx.renderer, 12.0f, 60.0f, line4);

        if (ctx.has_model_params && ctx.has_model_params()) {
            if (ctx.ensure_selected_param_index_valid) {
                ctx.ensure_selected_param_index_valid();
            }
            if (ctx.selected_param_index >= 0 &&
                ctx.selected_param_index < static_cast<int>(ctx.model->parameters.size())) {
                const ModelParameter &p = ctx.model->parameters[static_cast<std::size_t>(ctx.selected_param_index)];
                std::snprintf(line5, sizeof(line5), "Param[%d/%zu] %s = %.3f (target %.3f)",
                              ctx.selected_param_index + 1,
                              ctx.model->parameters.size(),
                              p.id.c_str(),
                              p.param.value(),
                              p.param.target());
                std::snprintf(line6, sizeof(line6),
                              "Keys: F1 debug | M manual | Tab switch | <-/-> fine | Up/Down coarse | Space reset | R reset all");
                SDL_RenderDebugText(ctx.renderer, 12.0f, 76.0f, line5);
                SDL_RenderDebugText(ctx.renderer, 12.0f, 92.0f, line6);
            }
        }
    }
}

}  // namespace

void RenderAppFrame(const AppRenderContext &ctx) {
    if (!ctx.renderer || !ctx.model) {
        return;
    }

    SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 128);
    SDL_RenderClear(ctx.renderer);

    if (ctx.model_loaded) {
        RenderModelRuntime(ctx.renderer,
                           ctx.model,
                           ctx.view_pan_x,
                           ctx.view_pan_y,
                           ctx.view_zoom);
    } else if (ctx.demo_texture) {
        SDL_FRect dest_rect;
        dest_rect.x = 50.0f;
        dest_rect.y = 50.0f;
        dest_rect.w = static_cast<float>(ctx.demo_texture_w);
        dest_rect.h = static_cast<float>(ctx.demo_texture_h);
        SDL_RenderTexture(ctx.renderer, ctx.demo_texture, nullptr, &dest_rect);
    }

    RenderPartEditorOverlay(ctx);
    RenderEditorStatus(ctx);
}

}  // namespace k2d

