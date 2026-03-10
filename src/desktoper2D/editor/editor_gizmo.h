#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/core/model.h"

namespace desktoper2D {

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

}  // namespace desktoper2D

