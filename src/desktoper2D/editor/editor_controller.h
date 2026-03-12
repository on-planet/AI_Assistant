#pragma once

#include "desktoper2D/editor/editor_commands.h"
#include "desktoper2D/editor/editor_gizmo.h"
#include "desktoper2D/editor/editor_types.h"
#include "desktoper2D/core/model.h"

#include <functional>
#include <utility>
#include <vector>

namespace desktoper2D {

    struct EditorControllerState {
        bool dragging_part = false;
        bool dragging_pivot = false;
        float drag_last_x = 0.0f;
        float drag_last_y = 0.0f;
        float drag_start_mouse_x = 0.0f;
        float drag_start_mouse_y = 0.0f;
        float drag_start_world_x = 0.0f;
        float drag_start_world_y = 0.0f;
        float drag_start_pos_x = 0.0f;
        float drag_start_pos_y = 0.0f;
        float drag_start_pivot_x = 0.0f;
        float drag_start_pivot_y = 0.0f;
    
        bool gizmo_dragging = false;
        GizmoHandle gizmo_hover_handle = GizmoHandle::None;
        GizmoHandle gizmo_active_handle = GizmoHandle::None;
        float gizmo_drag_start_mouse_x = 0.0f;
        float gizmo_drag_start_mouse_y = 0.0f;
        float gizmo_drag_start_pos_x = 0.0f;
        float gizmo_drag_start_pos_y = 0.0f;
        float gizmo_drag_start_world_x = 0.0f;
        float gizmo_drag_start_world_y = 0.0f;
        float gizmo_drag_start_rot_deg = 0.0f;
        float gizmo_drag_start_scale_x = 1.0f;
        float gizmo_drag_start_scale_y = 1.0f;
        float gizmo_drag_start_angle = 0.0f;
        float gizmo_drag_start_dist = 1.0f;

        bool edit_capture_active = false;
        EditCommand active_edit_cmd;

        std::vector<EditCommand> undo_stack;
        std::vector<EditCommand> redo_stack;
    };

struct EditorControllerContext {
    ModelRuntime *model = nullptr;
    int *selected_part_index = nullptr;

    AxisConstraint axis_constraint = AxisConstraint::None;
    bool snap_enabled = false;
    float snap_grid = 10.0f;
    float drag_sensitivity = 1.0f;
    float gizmo_sensitivity = 1.0f;
    std::function<void(bool, bool, float, float)> set_snap_indicator;

    std::function<void(ModelPart *, float, float)> apply_pivot_delta;
    std::function<bool()> has_model_parts;
    std::function<void()> ensure_selected_part_index_valid;
    std::function<int(float, float)> pick_top_part_at;
    std::function<int(float, float)> pick_cycle_next_part_at;
    std::function<std::pair<float, float>(const ModelRuntime &, int, float, float)> world_delta_to_parent_local;
    std::function<std::pair<float, float>(const ModelPart &, float, float)> world_delta_to_part_local;
};

void BeginCaptureEdit(EditorControllerState &state, const ModelPart &part);
void CommitCaptureEditIfChanged(EditorControllerState &state,
                                std::vector<EditCommand> &undo_stack,
                                std::vector<EditCommand> &redo_stack,
                                const ModelPart &part);
void CancelCaptureEdit(EditorControllerState &state);

void BeginDragPart(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y);
void BeginDragPivot(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y);
void EndDragging(EditorControllerState &state,
                 const EditorControllerContext &ctx,
                 std::vector<EditCommand> &undo_stack,
                 std::vector<EditCommand> &redo_stack);

void BeginGizmoDrag(EditorControllerState &state, const ModelPart &part, GizmoHandle handle, float mouse_x, float mouse_y);
void EndGizmoDrag(EditorControllerState &state,
                  const EditorControllerContext &ctx,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack);
void HandleGizmoDragMotion(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y);

void HandleEditorDragMotion(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y);

}  // namespace desktoper2D

