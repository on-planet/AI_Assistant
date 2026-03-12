#include "desktoper2D/editor/editor_gizmo.h"

#include <algorithm>
#include <cmath>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

namespace {

float DistanceSquared(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

void DrawCircleApprox(SDL_Renderer *renderer, float cx, float cy, float radius, int segments) {
    const int seg = std::max(12, segments);
    const float step = 2.0f * 3.14159265358979323846f / static_cast<float>(seg);
    float px = cx + radius;
    float py = cy;
    for (int i = 1; i <= seg; ++i) {
        const float a = step * static_cast<float>(i);
        const float x = cx + std::cos(a) * radius;
        const float y = cy + std::sin(a) * radius;
        SDL_RenderLine(renderer, px, py, x, y);
        px = x;
        py = y;
    }
}

}  // namespace

GizmoHandle PickGizmoHandle(const ModelPart &part, float mouse_x, float mouse_y) {
    const float cx = part.runtime_pos_x;
    const float cy = part.runtime_pos_y;

    const float axis_len = 52.0f;
    const float axis_hit_r = 9.0f;
    const float ring_r = 42.0f;
    const float ring_half_thickness = 6.0f;
    const float scale_handle_offset = 30.0f;
    const float scale_hit_r = 9.0f;

    if (DistanceSquared(mouse_x, mouse_y, cx + axis_len, cy) <= axis_hit_r * axis_hit_r) {
        return GizmoHandle::MoveX;
    }
    if (DistanceSquared(mouse_x, mouse_y, cx, cy - axis_len) <= axis_hit_r * axis_hit_r) {
        return GizmoHandle::MoveY;
    }

    const float dx = mouse_x - cx;
    const float dy = mouse_y - cy;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (std::abs(d - ring_r) <= ring_half_thickness) {
        return GizmoHandle::Rotate;
    }

    if (DistanceSquared(mouse_x, mouse_y, cx + scale_handle_offset, cy - scale_handle_offset) <= scale_hit_r * scale_hit_r) {
        return GizmoHandle::Scale;
    }

    return GizmoHandle::None;
}

void RenderGizmoOverlay(SDL_Renderer *renderer,
                        const ModelPart &part,
                        GizmoHandle hover,
                        GizmoHandle active,
                        AxisConstraint constraint) {
    const float cx = part.runtime_pos_x;
    const float cy = part.runtime_pos_y;

    const float axis_len = 52.0f;
    const float ring_r = 42.0f;
    const float scale_offset = 30.0f;

    const auto color_for = [constraint](GizmoHandle h, GizmoHandle hover_h, GizmoHandle active_h, SDL_Color base) {
        if (h == active_h) {
            return SDL_Color{255, 255, 255, 255};
        }
        if (h == hover_h) {
            return SDL_Color{255, 230, 120, 255};
        }
        if (constraint == AxisConstraint::XOnly && h == GizmoHandle::MoveX) {
            return SDL_Color{255, 210, 120, 255};
        }
        if (constraint == AxisConstraint::YOnly && h == GizmoHandle::MoveY) {
            return SDL_Color{255, 210, 120, 255};
        }
        return base;
    };

    const SDL_Color xcol = color_for(GizmoHandle::MoveX, hover, active, SDL_Color{255, 80, 80, 240});
    SDL_SetRenderDrawColor(renderer, xcol.r, xcol.g, xcol.b, xcol.a);
    SDL_RenderLine(renderer, cx, cy, cx + axis_len, cy);
    SDL_FRect xh{cx + axis_len - 4.0f, cy - 4.0f, 8.0f, 8.0f};
    SDL_RenderFillRect(renderer, &xh);

    const SDL_Color ycol = color_for(GizmoHandle::MoveY, hover, active, SDL_Color{80, 220, 120, 240});
    SDL_SetRenderDrawColor(renderer, ycol.r, ycol.g, ycol.b, ycol.a);
    SDL_RenderLine(renderer, cx, cy, cx, cy - axis_len);
    SDL_FRect yh{cx - 4.0f, cy - axis_len - 4.0f, 8.0f, 8.0f};
    SDL_RenderFillRect(renderer, &yh);

    const SDL_Color rcol = color_for(GizmoHandle::Rotate, hover, active, SDL_Color{96, 196, 255, 220});
    SDL_SetRenderDrawColor(renderer, rcol.r, rcol.g, rcol.b, rcol.a);
    DrawCircleApprox(renderer, cx, cy, ring_r, 40);

    const SDL_Color scol = color_for(GizmoHandle::Scale, hover, active, SDL_Color{220, 120, 255, 240});
    SDL_SetRenderDrawColor(renderer, scol.r, scol.g, scol.b, scol.a);
    SDL_FRect sh{cx + scale_offset - 6.0f, cy - scale_offset - 6.0f, 12.0f, 12.0f};
    SDL_RenderRect(renderer, &sh);
}

}  // namespace desktoper2D

