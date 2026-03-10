#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/editor/editor_gizmo.h"

namespace desktoper2D {

struct AppRuntime;
struct EditorControllerState;
struct ModelPart;

bool ComputePartAABB(const ModelPart &part, SDL_FRect *out_rect);
int PickTopPartAt(const AppRuntime &runtime, float x, float y);

void BeginDragPart(AppRuntime &runtime,
                   EditorControllerState &editor_state,
                   float mouse_x,
                   float mouse_y);

void BeginDragPivot(AppRuntime &runtime,
                    EditorControllerState &editor_state,
                    float mouse_x,
                    float mouse_y);

void EndDragging(AppRuntime &runtime, EditorControllerState &editor_state);

void BeginGizmoDrag(AppRuntime &runtime,
                    EditorControllerState &editor_state,
                    const ModelPart &part,
                    GizmoHandle handle,
                    float mouse_x,
                    float mouse_y);

void EndGizmoDrag(AppRuntime &runtime, EditorControllerState &editor_state);

void HandleGizmoDragMotion(AppRuntime &runtime,
                           EditorControllerState &editor_state,
                           float mouse_x,
                           float mouse_y);

void HandleEditorDragMotion(AppRuntime &runtime,
                            EditorControllerState &editor_state,
                            float mouse_x,
                            float mouse_y);

}  // namespace desktoper2D
