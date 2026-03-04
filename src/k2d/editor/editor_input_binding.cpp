#include "k2d/editor/editor_input_binding.h"

#include "k2d/controllers/app_input_controller.h"
#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/controllers/interaction_controller.h"

namespace k2d {

EditorInputCallbacks BuildEditorInputCallbacksFromRuntime(AppRuntime &runtime,
                                                          EditorInputBindingBridge bridge) {
    const auto ctx = AppInputControllerContext{
        .running = &runtime.running,
        .show_debug_stats = &runtime.show_debug_stats,
        .gui_enabled = &runtime.gui_enabled,
        .edit_mode = &runtime.edit_mode,
        .manual_param_mode = &runtime.manual_param_mode,
        .toggle_edit_mode = bridge.toggle_edit_mode,
        .toggle_manual_param_mode = bridge.toggle_manual_param_mode,
        .cycle_selected_part = bridge.cycle_selected_part,
        .adjust_selected_param = bridge.adjust_selected_param,
        .reset_selected_param = bridge.reset_selected_param,
        .reset_all_params = bridge.reset_all_params,
        .save_model = bridge.save_model,
        .undo_edit = bridge.undo_edit,
        .redo_edit = bridge.redo_edit,
        .on_mouse_button_down = [&runtime, bridge](float mx, float my, bool shift_pressed, Uint8 button) {
            if (button == SDL_BUTTON_LEFT) {
                bridge.OnHeadPatMouseDown(mx, my);
                bridge.EnsureSelectedPartIndexValid();
                if (!shift_pressed && runtime.selected_part_index >= 0 &&
                    runtime.selected_part_index < static_cast<int>(runtime.model.parts.size())) {
                    const ModelPart &selected = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)];
                    const GizmoHandle handle = k2d::PickGizmoHandle(selected, mx, my);
                    runtime.gizmo_hover_handle = handle;
                    if (handle != GizmoHandle::None) {
                        bridge.EndDragging();
                        bridge.BeginGizmoDrag(selected, handle, mx, my);
                        return;
                    }
                }
                bridge.EndGizmoDrag();
                if (shift_pressed) {
                    bridge.BeginDragPivot(mx, my);
                } else {
                    bridge.BeginDragPart(mx, my);
                }
            } else if (button == SDL_BUTTON_RIGHT) {
                bridge.EndGizmoDrag();
                bridge.BeginDragPivot(mx, my);
            }
        },
        .on_mouse_button_up = [bridge]() {
            bridge.EndDragging();
            bridge.EndGizmoDrag();
        },
        .on_mouse_motion = [&runtime, bridge](float mx, float my) {
            bridge.OnHeadPatMouseMotion(mx, my);

            if (runtime.gizmo_dragging) {
                bridge.HandleGizmoDragMotion(mx, my);
            } else {
                bridge.HandleEditorDragMotion(mx, my);
                bridge.EnsureSelectedPartIndexValid();
                if (runtime.selected_part_index >= 0 &&
                    runtime.selected_part_index < static_cast<int>(runtime.model.parts.size())) {
                    const ModelPart &selected = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)];
                    runtime.gizmo_hover_handle = k2d::PickGizmoHandle(selected, mx, my);
                } else {
                    runtime.gizmo_hover_handle = GizmoHandle::None;
                }
            }
        },
    };

    return BuildEditorInputCallbacks(ctx);
}

}  // namespace k2d
