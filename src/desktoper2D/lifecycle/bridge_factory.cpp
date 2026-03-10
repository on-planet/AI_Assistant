#include "desktoper2D/lifecycle/bridge_factory.h"

#include "desktoper2D/lifecycle/behavior_applier.h"
#include "desktoper2D/lifecycle/model_reload_service.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

EditorInputBindingBridge BuildEditorInputBindingBridge(const EditorInputBindingFactoryDeps &deps) {
    return EditorInputBindingBridge{
        .ensure_selected_part_index_valid = deps.ensure_selected_part_index_valid,
        .cycle_selected_part = deps.cycle_selected_part,
        .adjust_selected_param = deps.adjust_selected_param,
        .reset_selected_param = deps.reset_selected_param,
        .reset_all_params = deps.reset_all_params,
        .toggle_edit_mode = deps.toggle_edit_mode,
        .toggle_manual_param_mode = deps.toggle_manual_param_mode,
        .save_model = deps.save_model,
        .save_project = deps.save_project,
        .save_project_as = deps.save_project_as,
        .load_project = deps.load_project,
        .undo_edit = deps.undo_edit,
        .redo_edit = deps.redo_edit,
        .pick_top_part_at = deps.pick_top_part_at,
        .has_model_params = deps.has_model_params,
        .on_head_pat_mouse_motion = deps.on_head_pat_mouse_motion,
        .on_head_pat_mouse_down = deps.on_head_pat_mouse_down,
        .begin_drag_part = deps.begin_drag_part,
        .begin_drag_pivot = deps.begin_drag_pivot,
        .end_dragging = deps.end_dragging,
        .begin_gizmo_drag = deps.begin_gizmo_drag,
        .end_gizmo_drag = deps.end_gizmo_drag,
        .handle_gizmo_drag_motion = deps.handle_gizmo_drag_motion,
        .handle_editor_drag_motion = deps.handle_editor_drag_motion,
    };
}

AppEventHandlerBridge BuildAppEventHandlerBridge(const AppEventBridgeFactoryDeps &deps) {
    return AppEventHandlerBridge{
        .build_editor_input_callbacks = deps.build_editor_input_callbacks,
        .toggle_edit_mode = deps.toggle_edit_mode,
        .toggle_manual_param_mode = deps.toggle_manual_param_mode,
        .cycle_selected_part = deps.cycle_selected_part,
        .adjust_selected_param = deps.adjust_selected_param,
        .reset_selected_param = deps.reset_selected_param,
        .reset_all_params = deps.reset_all_params,
        .on_head_pat_mouse_motion = deps.on_head_pat_mouse_motion,
        .on_head_pat_mouse_down = deps.on_head_pat_mouse_down,
    };
}

RuntimeTickBridge BuildRuntimeTickBridge(const RuntimeTickBridgeFactoryDeps &deps) {
    return RuntimeTickBridge{
        .pick_top_part_at = deps.pick_top_part_at,
        .has_model_params = deps.has_model_params,
        .build_model_reload_context = deps.build_model_reload_context,
        .build_behavior_apply_context = deps.build_behavior_apply_context,
        .task_secondary_category_name = deps.task_secondary_category_name,
        .infer_task_category_inplace = deps.infer_task_category_inplace,
    };
}

RuntimeRenderBridge BuildRuntimeRenderBridge(const RuntimeRenderBridgeFactoryDeps &deps) {
    return RuntimeRenderBridge{
        .has_model_parts = deps.has_model_parts,
        .has_model_params = deps.has_model_params,
        .ensure_selected_part_index_valid = deps.ensure_selected_part_index_valid,
        .ensure_selected_param_index_valid = deps.ensure_selected_param_index_valid,
        .compute_part_aabb = deps.compute_part_aabb,
        .render_model_hierarchy_tree = deps.render_model_hierarchy_tree,
        .render_resource_tree_inspector = deps.render_resource_tree_inspector,
        .task_primary_category_name = deps.task_primary_category_name,
        .task_secondary_category_name = deps.task_secondary_category_name,
    };
}

}  // namespace desktoper2D
