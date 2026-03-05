#pragma once

#include <functional>

#include "k2d/editor/editor_input.h"
#include "k2d/editor/editor_gizmo.h"
#include "k2d/core/model.h"

namespace k2d {

struct AppRuntime;
struct InteractionControllerState;

struct EditorInputBindingBridge {
    using VoidFn = std::function<void()>;
    using BoolArgFn = std::function<void(bool)>;
    using FloatArgFn = std::function<void(float)>;
    using MouseFn = std::function<void(float, float)>;
    using PickTopPartAtFn = std::function<int(float, float)>;
    using HasModelParamsFn = std::function<bool()>;
    using GizmoBeginFn = std::function<void(const ModelPart &, GizmoHandle, float, float)>;

    VoidFn ensure_selected_part_index_valid;
    BoolArgFn cycle_selected_part;
    FloatArgFn adjust_selected_param;
    VoidFn reset_selected_param;
    VoidFn reset_all_params;
    VoidFn toggle_edit_mode;
    VoidFn toggle_manual_param_mode;
    VoidFn save_model;
    VoidFn save_project;
    VoidFn load_project;
    VoidFn undo_edit;
    VoidFn redo_edit;

    PickTopPartAtFn pick_top_part_at;
    HasModelParamsFn has_model_params;
    MouseFn on_head_pat_mouse_motion;
    MouseFn on_head_pat_mouse_down;

    MouseFn begin_drag_part;
    MouseFn begin_drag_pivot;
    VoidFn end_dragging;
    GizmoBeginFn begin_gizmo_drag;
    VoidFn end_gizmo_drag;
    MouseFn handle_gizmo_drag_motion;
    MouseFn handle_editor_drag_motion;

    void EnsureSelectedPartIndexValid() const {
        if (ensure_selected_part_index_valid) {
            ensure_selected_part_index_valid();
        }
    }

    void OnHeadPatMouseMotion(float mx, float my) const {
        if (on_head_pat_mouse_motion) {
            on_head_pat_mouse_motion(mx, my);
        }
    }

    void OnHeadPatMouseDown(float mx, float my) const {
        if (on_head_pat_mouse_down) {
            on_head_pat_mouse_down(mx, my);
        }
    }

    void BeginDragPart(float mx, float my) const {
        if (begin_drag_part) {
            begin_drag_part(mx, my);
        }
    }

    void BeginDragPivot(float mx, float my) const {
        if (begin_drag_pivot) {
            begin_drag_pivot(mx, my);
        }
    }

    void EndDragging() const {
        if (end_dragging) {
            end_dragging();
        }
    }

    void BeginGizmoDrag(const ModelPart &part, GizmoHandle handle, float mx, float my) const {
        if (begin_gizmo_drag) {
            begin_gizmo_drag(part, handle, mx, my);
        }
    }

    void EndGizmoDrag() const {
        if (end_gizmo_drag) {
            end_gizmo_drag();
        }
    }

    void HandleGizmoDragMotion(float mx, float my) const {
        if (handle_gizmo_drag_motion) {
            handle_gizmo_drag_motion(mx, my);
        }
    }

    void HandleEditorDragMotion(float mx, float my) const {
        if (handle_editor_drag_motion) {
            handle_editor_drag_motion(mx, my);
        }
    }
};

EditorInputCallbacks BuildEditorInputCallbacksFromRuntime(AppRuntime &runtime,
                                                          EditorInputBindingBridge bridge);

}  // namespace k2d
