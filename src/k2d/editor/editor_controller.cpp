#include "k2d/editor/editor_controller.h"

#include <algorithm>
#include <cmath>

namespace k2d {

void BeginCaptureEdit(EditorControllerState &state, const ModelPart &part) {
    state.edit_capture_active = true;
    state.active_edit_cmd = {};
    state.active_edit_cmd.type = EditCommand::Type::Transform;
    state.active_edit_cmd.part_id = part.id;
    state.active_edit_cmd.before_pos_x = part.base_pos_x;
    state.active_edit_cmd.before_pos_y = part.base_pos_y;
    state.active_edit_cmd.before_pivot_x = part.pivot_x;
    state.active_edit_cmd.before_pivot_y = part.pivot_y;
    state.active_edit_cmd.before_rot_deg = part.base_rot_deg;
    state.active_edit_cmd.before_scale_x = part.base_scale_x;
    state.active_edit_cmd.before_scale_y = part.base_scale_y;
}

void CommitCaptureEditIfChanged(EditorControllerState &state,
                                std::vector<EditCommand> &undo_stack,
                                std::vector<EditCommand> &redo_stack,
                                const ModelPart &part) {
    if (!state.edit_capture_active) {
        return;
    }

    state.active_edit_cmd.after_pos_x = part.base_pos_x;
    state.active_edit_cmd.after_pos_y = part.base_pos_y;
    state.active_edit_cmd.after_pivot_x = part.pivot_x;
    state.active_edit_cmd.after_pivot_y = part.pivot_y;
    state.active_edit_cmd.after_rot_deg = part.base_rot_deg;
    state.active_edit_cmd.after_scale_x = part.base_scale_x;
    state.active_edit_cmd.after_scale_y = part.base_scale_y;

    const EditCommand &cmd = state.active_edit_cmd;
    const bool changed =
        std::abs(cmd.after_pos_x - cmd.before_pos_x) > 1e-5f ||
        std::abs(cmd.after_pos_y - cmd.before_pos_y) > 1e-5f ||
        std::abs(cmd.after_pivot_x - cmd.before_pivot_x) > 1e-5f ||
        std::abs(cmd.after_pivot_y - cmd.before_pivot_y) > 1e-5f ||
        std::abs(cmd.after_rot_deg - cmd.before_rot_deg) > 1e-5f ||
        std::abs(cmd.after_scale_x - cmd.before_scale_x) > 1e-5f ||
        std::abs(cmd.after_scale_y - cmd.before_scale_y) > 1e-5f;

    if (changed) {
        PushEditCommand(undo_stack, redo_stack, state.active_edit_cmd);
    }

    state.edit_capture_active = false;
}

void CancelCaptureEdit(EditorControllerState &state) {
    state.edit_capture_active = false;
}

void BeginDragPart(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y) {
    if (!ctx.model || !ctx.selected_part_index || !ctx.pick_top_part_at) {
        return;
    }

    const int picked = ctx.pick_top_part_at(mouse_x, mouse_y);
    if (picked < 0 || picked >= static_cast<int>(ctx.model->parts.size())) {
        return;
    }

    *ctx.selected_part_index = picked;
    BeginCaptureEdit(state, ctx.model->parts[static_cast<std::size_t>(picked)]);
    state.dragging_part = true;
    state.dragging_pivot = false;
    state.drag_last_x = mouse_x;
    state.drag_last_y = mouse_y;
}

void BeginDragPivot(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y) {
    if (!ctx.model || !ctx.selected_part_index || !ctx.has_model_parts || !ctx.pick_top_part_at) {
        return;
    }
    if (!ctx.has_model_parts()) {
        return;
    }

    if (*ctx.selected_part_index < 0 || *ctx.selected_part_index >= static_cast<int>(ctx.model->parts.size())) {
        const int picked = ctx.pick_top_part_at(mouse_x, mouse_y);
        if (picked < 0 || picked >= static_cast<int>(ctx.model->parts.size())) {
            return;
        }
        *ctx.selected_part_index = picked;
    }

    BeginCaptureEdit(state, ctx.model->parts[static_cast<std::size_t>(*ctx.selected_part_index)]);
    state.dragging_pivot = true;
    state.dragging_part = false;
    state.drag_last_x = mouse_x;
    state.drag_last_y = mouse_y;
}

void EndDragging(EditorControllerState &state,
                 const EditorControllerContext &ctx,
                 std::vector<EditCommand> &undo_stack,
                 std::vector<EditCommand> &redo_stack) {
    if (ctx.model && ctx.selected_part_index && ctx.has_model_parts &&
        (state.dragging_part || state.dragging_pivot) && ctx.has_model_parts() &&
        *ctx.selected_part_index >= 0 && *ctx.selected_part_index < static_cast<int>(ctx.model->parts.size())) {
        CommitCaptureEditIfChanged(state,
                                   undo_stack,
                                   redo_stack,
                                   ctx.model->parts[static_cast<std::size_t>(*ctx.selected_part_index)]);
    } else {
        CancelCaptureEdit(state);
    }

    state.dragging_part = false;
    state.dragging_pivot = false;
}

void BeginGizmoDrag(EditorControllerState &state, const ModelPart &part, GizmoHandle handle, float mouse_x, float mouse_y) {
    BeginCaptureEdit(state, part);
    state.gizmo_dragging = true;
    state.gizmo_active_handle = handle;
    state.gizmo_drag_start_mouse_x = mouse_x;
    state.gizmo_drag_start_mouse_y = mouse_y;
    state.gizmo_drag_start_pos_x = part.base_pos_x;
    state.gizmo_drag_start_pos_y = part.base_pos_y;
    state.gizmo_drag_start_rot_deg = part.base_rot_deg;
    state.gizmo_drag_start_scale_x = part.base_scale_x;
    state.gizmo_drag_start_scale_y = part.base_scale_y;

    const float cx = part.runtime_pos_x;
    const float cy = part.runtime_pos_y;
    const float vx = mouse_x - cx;
    const float vy = mouse_y - cy;
    state.gizmo_drag_start_angle = std::atan2(vy, vx);
    state.gizmo_drag_start_dist = std::max(1.0f, std::sqrt(vx * vx + vy * vy));
}

void EndGizmoDrag(EditorControllerState &state,
                  const EditorControllerContext &ctx,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack) {
    if (ctx.model && ctx.selected_part_index && ctx.has_model_parts && state.gizmo_dragging &&
        ctx.has_model_parts() &&
        *ctx.selected_part_index >= 0 && *ctx.selected_part_index < static_cast<int>(ctx.model->parts.size())) {
        CommitCaptureEditIfChanged(state,
                                   undo_stack,
                                   redo_stack,
                                   ctx.model->parts[static_cast<std::size_t>(*ctx.selected_part_index)]);
    } else {
        CancelCaptureEdit(state);
    }

    state.gizmo_dragging = false;
    state.gizmo_active_handle = GizmoHandle::None;
}

void HandleGizmoDragMotion(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y) {
    if (!ctx.model || !ctx.selected_part_index || !ctx.has_model_parts || !ctx.ensure_selected_part_index_valid ||
        !ctx.world_delta_to_parent_local) {
        return;
    }

    if (!state.gizmo_dragging || !ctx.has_model_parts()) {
        return;
    }

    ctx.ensure_selected_part_index_valid();
    if (*ctx.selected_part_index < 0 || *ctx.selected_part_index >= static_cast<int>(ctx.model->parts.size())) {
        state.gizmo_dragging = false;
        state.gizmo_active_handle = GizmoHandle::None;
        CancelCaptureEdit(state);
        return;
    }

    ModelPart &part = ctx.model->parts[static_cast<std::size_t>(*ctx.selected_part_index)];

    const float mx0 = state.gizmo_drag_start_mouse_x;
    const float my0 = state.gizmo_drag_start_mouse_y;
    const float dx_world = mouse_x - mx0;
    const float dy_world = mouse_y - my0;

    switch (state.gizmo_active_handle) {
        case GizmoHandle::MoveX: {
            const auto local_delta = ctx.world_delta_to_parent_local(*ctx.model, part.parent_index, dx_world, 0.0f);
            part.base_pos_x = state.gizmo_drag_start_pos_x + local_delta.first;
            part.base_pos_y = state.gizmo_drag_start_pos_y + local_delta.second;
            break;
        }
        case GizmoHandle::MoveY: {
            const auto local_delta = ctx.world_delta_to_parent_local(*ctx.model, part.parent_index, 0.0f, dy_world);
            part.base_pos_x = state.gizmo_drag_start_pos_x + local_delta.first;
            part.base_pos_y = state.gizmo_drag_start_pos_y + local_delta.second;
            break;
        }
        case GizmoHandle::Rotate: {
            const float cx = part.runtime_pos_x;
            const float cy = part.runtime_pos_y;
            const float angle = std::atan2(mouse_y - cy, mouse_x - cx);
            const float delta_rad = angle - state.gizmo_drag_start_angle;
            const float delta_deg = delta_rad * 180.0f / 3.14159265358979323846f;
            part.base_rot_deg = state.gizmo_drag_start_rot_deg + delta_deg;
            break;
        }
        case GizmoHandle::Scale: {
            const float cx = part.runtime_pos_x;
            const float cy = part.runtime_pos_y;
            const float vx = mouse_x - cx;
            const float vy = mouse_y - cy;
            const float dist = std::max(1.0f, std::sqrt(vx * vx + vy * vy));
            const float factor = std::clamp(dist / std::max(1.0f, state.gizmo_drag_start_dist), 0.1f, 8.0f);
            part.base_scale_x = std::max(0.05f, state.gizmo_drag_start_scale_x * factor);
            part.base_scale_y = std::max(0.05f, state.gizmo_drag_start_scale_y * factor);
            break;
        }
        default:
            break;
    }
}

void HandleEditorDragMotion(EditorControllerState &state, const EditorControllerContext &ctx, float mouse_x, float mouse_y) {
    if (!ctx.model || !ctx.selected_part_index || !ctx.has_model_parts ||
        !ctx.world_delta_to_parent_local || !ctx.world_delta_to_part_local || !ctx.apply_pivot_delta) {
        return;
    }

    if (!ctx.has_model_parts()) {
        state.dragging_part = false;
        state.dragging_pivot = false;
        CancelCaptureEdit(state);
        return;
    }

    if (!(state.dragging_part || state.dragging_pivot)) {
        return;
    }

    if (*ctx.selected_part_index < 0 || *ctx.selected_part_index >= static_cast<int>(ctx.model->parts.size())) {
        state.dragging_part = false;
        state.dragging_pivot = false;
        CancelCaptureEdit(state);
        return;
    }

    const float dx = mouse_x - state.drag_last_x;
    const float dy = mouse_y - state.drag_last_y;
    state.drag_last_x = mouse_x;
    state.drag_last_y = mouse_y;

    ModelPart &part = ctx.model->parts[static_cast<std::size_t>(*ctx.selected_part_index)];

    if (state.dragging_part) {
        const auto local_delta = ctx.world_delta_to_parent_local(*ctx.model, part.parent_index, dx, dy);
        part.base_pos_x += local_delta.first;
        part.base_pos_y += local_delta.second;
    } else if (state.dragging_pivot) {
        const auto local_delta = ctx.world_delta_to_part_local(part, dx, dy);
        ctx.apply_pivot_delta(&part, local_delta.first, local_delta.second);
    }
}

}  // namespace k2d

