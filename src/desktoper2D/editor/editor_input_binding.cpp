#include "desktoper2D/editor/editor_input_binding.h"

#include "desktoper2D/controllers/app_input_controller.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/controllers/interaction_controller.h"

#include <string>

namespace desktoper2D {

EditorInputCallbacks BuildEditorInputCallbacksFromRuntime(AppRuntime &runtime,
                                                          EditorInputBindingBridge bridge) {
    auto set_editor_status = [&runtime](const std::string &text, float ttl_sec) {
        runtime.editor_status = text;
        runtime.editor_status_ttl = std::max(0.0f, ttl_sec);
    };

    const auto ctx = AppInputControllerContext{
        .running = &runtime.running,
        .show_debug_stats = &runtime.show_debug_stats,
        .gui_enabled = &runtime.gui_enabled,
        .edit_mode = &runtime.edit_mode,
        .manual_param_mode = &runtime.manual_param_mode,
        .toggle_edit_mode = [&runtime, bridge, set_editor_status]() {
            if (bridge.toggle_edit_mode) {
                bridge.toggle_edit_mode();
                if (runtime.edit_mode && !runtime.manual_param_mode) {
                    set_editor_status("进入编辑态：参数仍由运行态驱动", 2.5f);
                } else if (runtime.edit_mode && runtime.manual_param_mode) {
                    set_editor_status("编辑态 + 手动参数：将覆盖运行态/动画通道参数", 2.5f);
                } else {
                    set_editor_status("退出编辑态：回到运行态驱动", 2.0f);
                }
            }
        },
        .toggle_manual_param_mode = [&runtime, bridge, set_editor_status]() {
            if (bridge.toggle_manual_param_mode) {
                bridge.toggle_manual_param_mode();
                if (runtime.manual_param_mode && !runtime.edit_mode) {
                    set_editor_status("手动参数开启：运行态参数将被手动编辑覆盖", 2.5f);
                } else if (runtime.manual_param_mode) {
                    set_editor_status("编辑态 + 手动参数：将覆盖运行态/动画通道参数", 2.5f);
                } else {
                    set_editor_status("手动参数关闭：回到运行态参数驱动", 2.0f);
                }
            }
        },
        .cycle_selected_part = bridge.cycle_selected_part,
        .adjust_selected_param = [&runtime, bridge, set_editor_status](float delta) {
            if (bridge.adjust_selected_param) {
                bridge.adjust_selected_param(delta);
                if (!runtime.edit_mode && runtime.manual_param_mode) {
                    set_editor_status("运行态参数被手动输入覆盖", 1.8f);
                }
            }
        },
        .reset_selected_param = [&runtime, bridge, set_editor_status]() {
            if (bridge.reset_selected_param) {
                bridge.reset_selected_param();
                if (!runtime.edit_mode && runtime.manual_param_mode) {
                    set_editor_status("运行态参数重置：仍处于手动覆盖", 1.8f);
                }
            }
        },
        .reset_all_params = [&runtime, bridge, set_editor_status]() {
            if (bridge.reset_all_params) {
                bridge.reset_all_params();
                if (!runtime.edit_mode && runtime.manual_param_mode) {
                    set_editor_status("运行态参数重置：仍处于手动覆盖", 1.8f);
                }
            }
        },
        .save_model = bridge.save_model,
        .save_project = bridge.save_project,
        .save_project_as = bridge.save_project_as,
        .load_project = bridge.load_project,
        .undo_edit = bridge.undo_edit,
        .redo_edit = bridge.redo_edit,
        .on_mouse_button_down = [&runtime, bridge](float mx, float my, bool shift_pressed, Uint8 button) {
            if (button == SDL_BUTTON_LEFT) {
                bridge.OnHeadPatMouseDown(mx, my);
                bridge.EnsureSelectedPartIndexValid();
                if (!shift_pressed && runtime.selected_part_index >= 0 &&
                    runtime.selected_part_index < static_cast<int>(runtime.model.parts.size())) {
                    const ModelPart &selected = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)];
                    const GizmoHandle handle = desktoper2D::PickGizmoHandle(selected, mx, my);
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
                    if (runtime.pick_cycle_enabled) {
                        runtime.pick_cycle_offset += 1;
                    }
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
                    runtime.gizmo_hover_handle = desktoper2D::PickGizmoHandle(selected, mx, my);
                } else {
                    runtime.gizmo_hover_handle = GizmoHandle::None;
                }
            }
        },
    };

    return BuildEditorInputCallbacks(ctx);
}

}  // namespace desktoper2D
