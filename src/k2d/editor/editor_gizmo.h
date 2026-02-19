#pragma once

#include <SDL3/SDL.h>

#include "k2d/core/model.h"

namespace k2d {

enum class GizmoHandle {
    None,
    MoveX,
    MoveY,
    Rotate,
    Scale,
};

GizmoHandle PickGizmoHandle(const ModelPart &part, float mouse_x, float mouse_y);

void RenderGizmoOverlay(SDL_Renderer *renderer,
                        const ModelPart &part,
                        GizmoHandle hover,
                        GizmoHandle active);

}  // namespace k2d

