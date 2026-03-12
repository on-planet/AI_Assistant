#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/core/model.h"
#include "desktoper2D/editor/editor_types.h"

namespace desktoper2D {

GizmoHandle PickGizmoHandle(const ModelPart &part, float mouse_x, float mouse_y);

void RenderGizmoOverlay(SDL_Renderer *renderer,
                        const ModelPart &part,
                        GizmoHandle hover,
                        GizmoHandle active,
                        AxisConstraint constraint);

}  // namespace desktoper2D

