#include "desktoper2D/lifecycle/editor/editor_interaction_facade.h"

#include "desktoper2D/editor/editor_controller.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace desktoper2D {

int PickCycleNextPartAt(const AppRuntime &runtime, float x, float y);

namespace {

bool HasModelParts(const AppRuntime &runtime) {
    return runtime.model_loaded && !runtime.model.parts.empty();
}

void EnsureSelectedPartIndexValid(AppRuntime &runtime) {
    if (!HasModelParts(runtime)) {
        runtime.selected_part_index = -1;
        return;
    }
    const int count = static_cast<int>(runtime.model.parts.size());
    if (runtime.selected_part_index < 0 || runtime.selected_part_index >= count) {
        runtime.selected_part_index = 0;
    }
}

bool PointInTriangle(float px, float py,
                     float ax, float ay,
                     float bx, float by,
                     float cx, float cy) {
    const float v0x = cx - ax;
    const float v0y = cy - ay;
    const float v1x = bx - ax;
    const float v1y = by - ay;
    const float v2x = px - ax;
    const float v2y = py - ay;

    const float dot00 = v0x * v0x + v0y * v0y;
    const float dot01 = v0x * v1x + v0y * v1y;
    const float dot02 = v0x * v2x + v0y * v2y;
    const float dot11 = v1x * v1x + v1y * v1y;
    const float dot12 = v1x * v2x + v1y * v2y;

    const float denom = dot00 * dot11 - dot01 * dot01;
    if (std::abs(denom) < 1e-8f) {
        return false;
    }

    const float inv = 1.0f / denom;
    const float u = (dot11 * dot02 - dot01 * dot12) * inv;
    const float v = (dot00 * dot12 - dot01 * dot02) * inv;
    return (u >= 0.0f) && (v >= 0.0f) && (u + v <= 1.0f);
}

bool PartContainsPointPrecise(const ModelPart &part, float x, float y) {
    if (part.deformed_positions.size() != part.mesh.positions.size()) {
        return false;
    }
    if (part.deformed_positions.size() < 6 || part.mesh.indices.size() < 3) {
        return false;
    }

    for (std::size_t i = 0; i + 2 < part.mesh.indices.size(); i += 3) {
        const std::size_t i0 = static_cast<std::size_t>(part.mesh.indices[i]) * 2;
        const std::size_t i1 = static_cast<std::size_t>(part.mesh.indices[i + 1]) * 2;
        const std::size_t i2 = static_cast<std::size_t>(part.mesh.indices[i + 2]) * 2;

        if (i2 + 1 >= part.deformed_positions.size()) {
            continue;
        }

        const float ax = part.deformed_positions[i0];
        const float ay = part.deformed_positions[i0 + 1];
        const float bx = part.deformed_positions[i1];
        const float by = part.deformed_positions[i1 + 1];
        const float cx = part.deformed_positions[i2];
        const float cy = part.deformed_positions[i2 + 1];

        if (PointInTriangle(x, y, ax, ay, bx, by, cx, cy)) {
            return true;
        }
    }

    return false;
}

std::string NormalizePickFilter(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }),
               text.end());
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool HasPickFilter(const AppRuntime &runtime) {
    return runtime.pick_name_filter_enabled && runtime.pick_name_filter[0] != '\0';
}

bool PassPickNameFilter(const AppRuntime &runtime, const ModelPart &part) {
    if (!HasPickFilter(runtime)) {
        return true;
    }
    const std::string needle = NormalizePickFilter(runtime.pick_name_filter);
    if (needle.empty()) {
        return true;
    }
    const std::string id = NormalizePickFilter(part.id);
    if (id.find(needle) != std::string::npos) {
        return true;
    }
    if (!part.texture_cache_key.empty()) {
        const std::string texture_key = NormalizePickFilter(part.texture_cache_key);
        if (texture_key.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool IsDescendantOrSelf(const ModelRuntime &model, int candidate_idx, int root_idx) {
    if (candidate_idx < 0 || candidate_idx >= static_cast<int>(model.parts.size())) {
        return false;
    }
    if (root_idx < 0 || root_idx >= static_cast<int>(model.parts.size())) {
        return false;
    }
    int walk = candidate_idx;
    while (walk >= 0 && walk < static_cast<int>(model.parts.size())) {
        if (walk == root_idx) {
            return true;
        }
        walk = model.parts[static_cast<std::size_t>(walk)].parent_index;
    }
    return false;
}

bool PassPickScopeFilter(const AppRuntime &runtime, int idx) {
    if (!runtime.pick_scope_filter_enabled) {
        return true;
    }
    if (idx < 0 || idx >= static_cast<int>(runtime.model.parts.size())) {
        return false;
    }
    const int selected = runtime.selected_part_index;
    if (selected < 0 || selected >= static_cast<int>(runtime.model.parts.size())) {
        return false;
    }
    if (runtime.pick_scope_mode == 1) {
        return idx == selected;
    }
    if (runtime.pick_scope_mode == 2) {
        return IsDescendantOrSelf(runtime.model, idx, selected);
    }
    return true;
}

bool PassPickLockFilter(const AppRuntime &runtime, const ModelPart &part) {
    if (!runtime.pick_lock_filter_enabled) {
        return true;
    }
    return !part.pick_locked;
}

bool PassPickFilters(const AppRuntime &runtime, int idx, const ModelPart &part) {
    if (part.runtime_opacity <= 0.01f) {
        return false;
    }
    if (!PassPickLockFilter(runtime, part)) {
        return false;
    }
    if (!PassPickScopeFilter(runtime, idx)) {
        return false;
    }
    if (!PassPickNameFilter(runtime, part)) {
        return false;
    }
    return true;
}

std::pair<float, float> WorldDeltaToParentLocal(const ModelRuntime &model, int parent_index, float dx, float dy) {
    if (parent_index < 0 || parent_index >= static_cast<int>(model.parts.size())) {
        return {dx, dy};
    }

    const ModelPart &parent = model.parts[static_cast<std::size_t>(parent_index)];
    const float pr = parent.runtime_rot_deg * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(pr);
    const float s = std::sin(pr);
    const float sx = std::max(0.001f, std::abs(parent.runtime_scale_x));
    const float sy = std::max(0.001f, std::abs(parent.runtime_scale_y));

    const float local_x = (dx * c + dy * s) / sx;
    const float local_y = (-dx * s + dy * c) / sy;
    return {local_x, local_y};
}

std::pair<float, float> WorldDeltaToPartLocal(const ModelPart &part, float dx, float dy) {
    const float pr = part.runtime_rot_deg * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(pr);
    const float s = std::sin(pr);
    const float sx = std::max(0.001f, std::abs(part.runtime_scale_x));
    const float sy = std::max(0.001f, std::abs(part.runtime_scale_y));

    const float local_x = (dx * c + dy * s) / sx;
    const float local_y = (-dx * s + dy * c) / sy;
    return {local_x, local_y};
}

void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y) {
    if (!part) {
        return;
    }

    part->pivot_x += delta_x;
    part->pivot_y += delta_y;

    part->base_pos_x += delta_x;
    part->base_pos_y += delta_y;

    for (std::size_t i = 0; i + 1 < part->mesh.positions.size(); i += 2) {
        part->mesh.positions[i] -= delta_x;
        part->mesh.positions[i + 1] -= delta_y;
    }

    if (part->deformed_positions.size() == part->mesh.positions.size()) {
        for (std::size_t i = 0; i + 1 < part->deformed_positions.size(); i += 2) {
            part->deformed_positions[i] -= delta_x;
            part->deformed_positions[i + 1] -= delta_y;
        }
    }
}

void SyncEditorControllerStateFromApp(const AppRuntime &runtime, EditorControllerState &editor_state) {
    editor_state.dragging_part = runtime.dragging_part;
    editor_state.dragging_pivot = runtime.dragging_pivot;
    editor_state.drag_last_x = runtime.drag_last_x;
    editor_state.drag_last_y = runtime.drag_last_y;
    editor_state.drag_start_mouse_x = runtime.drag_start_mouse_x;
    editor_state.drag_start_mouse_y = runtime.drag_start_mouse_y;
    editor_state.drag_start_world_x = runtime.drag_start_world_x;
    editor_state.drag_start_world_y = runtime.drag_start_world_y;
    editor_state.drag_start_pos_x = runtime.drag_start_pos_x;
    editor_state.drag_start_pos_y = runtime.drag_start_pos_y;
    editor_state.drag_start_pivot_x = runtime.drag_start_pivot_x;
    editor_state.drag_start_pivot_y = runtime.drag_start_pivot_y;

    editor_state.gizmo_dragging = runtime.gizmo_dragging;
    editor_state.gizmo_hover_handle = runtime.gizmo_hover_handle;
    editor_state.gizmo_active_handle = runtime.gizmo_active_handle;
    editor_state.gizmo_drag_start_mouse_x = runtime.gizmo_drag_start_mouse_x;
    editor_state.gizmo_drag_start_mouse_y = runtime.gizmo_drag_start_mouse_y;
    editor_state.gizmo_drag_start_pos_x = runtime.gizmo_drag_start_pos_x;
    editor_state.gizmo_drag_start_pos_y = runtime.gizmo_drag_start_pos_y;
    editor_state.gizmo_drag_start_world_x = runtime.gizmo_drag_start_world_x;
    editor_state.gizmo_drag_start_world_y = runtime.gizmo_drag_start_world_y;
    editor_state.gizmo_drag_start_rot_deg = runtime.gizmo_drag_start_rot_deg;
    editor_state.gizmo_drag_start_scale_x = runtime.gizmo_drag_start_scale_x;
    editor_state.gizmo_drag_start_scale_y = runtime.gizmo_drag_start_scale_y;
    editor_state.gizmo_drag_start_angle = runtime.gizmo_drag_start_angle;
    editor_state.gizmo_drag_start_dist = runtime.gizmo_drag_start_dist;

    editor_state.edit_capture_active = runtime.edit_capture_active;
    editor_state.active_edit_cmd = runtime.active_edit_cmd;
}

void SyncAppStateFromEditorController(AppRuntime &runtime, const EditorControllerState &editor_state) {
    runtime.dragging_part = editor_state.dragging_part;
    runtime.dragging_pivot = editor_state.dragging_pivot;
    runtime.drag_last_x = editor_state.drag_last_x;
    runtime.drag_last_y = editor_state.drag_last_y;
    runtime.drag_start_mouse_x = editor_state.drag_start_mouse_x;
    runtime.drag_start_mouse_y = editor_state.drag_start_mouse_y;
    runtime.drag_start_world_x = editor_state.drag_start_world_x;
    runtime.drag_start_world_y = editor_state.drag_start_world_y;
    runtime.drag_start_pos_x = editor_state.drag_start_pos_x;
    runtime.drag_start_pos_y = editor_state.drag_start_pos_y;
    runtime.drag_start_pivot_x = editor_state.drag_start_pivot_x;
    runtime.drag_start_pivot_y = editor_state.drag_start_pivot_y;

    runtime.gizmo_dragging = editor_state.gizmo_dragging;
    runtime.gizmo_hover_handle = editor_state.gizmo_hover_handle;
    runtime.gizmo_active_handle = editor_state.gizmo_active_handle;
    runtime.gizmo_drag_start_mouse_x = editor_state.gizmo_drag_start_mouse_x;
    runtime.gizmo_drag_start_mouse_y = editor_state.gizmo_drag_start_mouse_y;
    runtime.gizmo_drag_start_pos_x = editor_state.gizmo_drag_start_pos_x;
    runtime.gizmo_drag_start_pos_y = editor_state.gizmo_drag_start_pos_y;
    runtime.gizmo_drag_start_world_x = editor_state.gizmo_drag_start_world_x;
    runtime.gizmo_drag_start_world_y = editor_state.gizmo_drag_start_world_y;
    runtime.gizmo_drag_start_rot_deg = editor_state.gizmo_drag_start_rot_deg;
    runtime.gizmo_drag_start_scale_x = editor_state.gizmo_drag_start_scale_x;
    runtime.gizmo_drag_start_scale_y = editor_state.gizmo_drag_start_scale_y;
    runtime.gizmo_drag_start_angle = editor_state.gizmo_drag_start_angle;
    runtime.gizmo_drag_start_dist = editor_state.gizmo_drag_start_dist;

    runtime.edit_capture_active = editor_state.edit_capture_active;
    runtime.active_edit_cmd = editor_state.active_edit_cmd;
}

EditorControllerContext BuildEditorControllerContext(AppRuntime &runtime) {
    EditorControllerContext ctx{};
    ctx.model = &runtime.model;
    ctx.selected_part_index = &runtime.selected_part_index;
    ctx.axis_constraint = runtime.axis_constraint;
    ctx.snap_enabled = runtime.snap_enabled;
    ctx.snap_grid = runtime.snap_grid;
    ctx.drag_sensitivity = runtime.editor_drag_sensitivity;
    ctx.gizmo_sensitivity = runtime.editor_gizmo_sensitivity;
    ctx.set_snap_indicator = [&runtime](bool snap_x, bool snap_y, float world_x, float world_y) {
        runtime.editor_snap_active_x = snap_x;
        runtime.editor_snap_active_y = snap_y;
        runtime.editor_snap_world_x = world_x;
        runtime.editor_snap_world_y = world_y;
    };
    ctx.apply_pivot_delta = [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); };
    ctx.has_model_parts = [&runtime]() { return HasModelParts(runtime); };
    ctx.ensure_selected_part_index_valid = [&runtime]() { EnsureSelectedPartIndexValid(runtime); };
    ctx.pick_top_part_at = [&runtime](float x, float y) { return PickTopPartAt(runtime, x, y); };
    ctx.pick_cycle_next_part_at = [&runtime](float x, float y) { return desktoper2D::PickCycleNextPartAt(runtime, x, y); };
    ctx.world_delta_to_parent_local = [](const ModelRuntime &model, int parent_index, float dx, float dy) {
        return WorldDeltaToParentLocal(model, parent_index, dx, dy);
    };
    ctx.world_delta_to_part_local = [](const ModelPart &part, float dx, float dy) {
        return WorldDeltaToPartLocal(part, dx, dy);
    };
    return ctx;
}

}  // namespace

bool ComputePartAABB(const ModelPart &part, SDL_FRect *out_rect) {
    if (!out_rect) {
        return false;
    }
    if (part.deformed_positions.size() < 2) {
        return false;
    }

    float min_x = std::numeric_limits<float>::infinity();
    float min_y = std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();

    for (std::size_t i = 0; i + 1 < part.deformed_positions.size(); i += 2) {
        const float x = part.deformed_positions[i];
        const float y = part.deformed_positions[i + 1];
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }

    if (!std::isfinite(min_x) || !std::isfinite(min_y) || !std::isfinite(max_x) || !std::isfinite(max_y)) {
        return false;
    }

    out_rect->x = min_x;
    out_rect->y = min_y;
    out_rect->w = std::max(1.0f, max_x - min_x);
    out_rect->h = std::max(1.0f, max_y - min_y);
    return true;
}

int PickTopPartAt(const AppRuntime &runtime, float x, float y) {
    if (!HasModelParts(runtime)) {
        return -1;
    }

    for (auto it = runtime.model.draw_order_indices.rbegin();
         it != runtime.model.draw_order_indices.rend(); ++it) {
        const int idx = *it;
        if (idx < 0 || idx >= static_cast<int>(runtime.model.parts.size())) {
            continue;
        }

        const ModelPart &part = runtime.model.parts[static_cast<std::size_t>(idx)];
        if (!PassPickFilters(runtime, idx, part)) {
            continue;
        }

        if (PartContainsPointPrecise(part, x, y)) {
            return idx;
        }
    }

    return -1;
}

std::vector<int> CollectPickCandidates(const AppRuntime &runtime, float x, float y) {
    std::vector<int> candidates;
    if (!HasModelParts(runtime)) {
        return candidates;
    }

    for (auto it = runtime.model.draw_order_indices.rbegin();
         it != runtime.model.draw_order_indices.rend(); ++it) {
        const int idx = *it;
        if (idx < 0 || idx >= static_cast<int>(runtime.model.parts.size())) {
            continue;
        }

        const ModelPart &part = runtime.model.parts[static_cast<std::size_t>(idx)];
        if (!PassPickFilters(runtime, idx, part)) {
            continue;
        }

        if (PartContainsPointPrecise(part, x, y)) {
            candidates.push_back(idx);
        }
    }

    return candidates;
}

int PickCycleNextPartAt(const AppRuntime &runtime, float x, float y) {
    if (!runtime.pick_cycle_enabled) {
        return PickTopPartAt(runtime, x, y);
    }

    std::vector<int> candidates = CollectPickCandidates(runtime, x, y);
    if (candidates.empty()) {
        return -1;
    }

    const int offset = runtime.pick_cycle_offset % static_cast<int>(candidates.size());
    const int index = (offset + static_cast<int>(candidates.size())) % static_cast<int>(candidates.size());
    return candidates[static_cast<std::size_t>(index)];
}

void BeginDragPart(AppRuntime &runtime,
                   EditorControllerState &editor_state,
                   float mouse_x,
                   float mouse_y) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    const auto ctx = BuildEditorControllerContext(runtime);
    desktoper2D::BeginDragPart(editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController(runtime, editor_state);
}

void BeginDragPivot(AppRuntime &runtime,
                    EditorControllerState &editor_state,
                    float mouse_x,
                    float mouse_y) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    const auto ctx = BuildEditorControllerContext(runtime);
    desktoper2D::BeginDragPivot(editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController(runtime, editor_state);
}

void EndDragging(AppRuntime &runtime, EditorControllerState &editor_state) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    const auto ctx = BuildEditorControllerContext(runtime);
    desktoper2D::EndDragging(editor_state, ctx, runtime.undo_stack, runtime.redo_stack);
    SyncAppStateFromEditorController(runtime, editor_state);
}

void BeginGizmoDrag(AppRuntime &runtime,
                    EditorControllerState &editor_state,
                    const ModelPart &part,
                    GizmoHandle handle,
                    float mouse_x,
                    float mouse_y) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    desktoper2D::BeginGizmoDrag(editor_state, part, handle, mouse_x, mouse_y);
    SyncAppStateFromEditorController(runtime, editor_state);
}

void EndGizmoDrag(AppRuntime &runtime, EditorControllerState &editor_state) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    const auto ctx = BuildEditorControllerContext(runtime);
    desktoper2D::EndGizmoDrag(editor_state, ctx, runtime.undo_stack, runtime.redo_stack);
    SyncAppStateFromEditorController(runtime, editor_state);
}

void HandleGizmoDragMotion(AppRuntime &runtime,
                           EditorControllerState &editor_state,
                           float mouse_x,
                           float mouse_y) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    const auto ctx = BuildEditorControllerContext(runtime);
    desktoper2D::HandleGizmoDragMotion(editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController(runtime, editor_state);
}

void HandleEditorDragMotion(AppRuntime &runtime,
                            EditorControllerState &editor_state,
                            float mouse_x,
                            float mouse_y) {
    SyncEditorControllerStateFromApp(runtime, editor_state);
    const auto ctx = BuildEditorControllerContext(runtime);
    desktoper2D::HandleEditorDragMotion(editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController(runtime, editor_state);
}

}  // namespace desktoper2D
