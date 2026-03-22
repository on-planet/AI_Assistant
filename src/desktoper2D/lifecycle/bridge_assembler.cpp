#include "desktoper2D/lifecycle/bridge_assembler.h"

#include <SDL3/SDL.h>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/state/runtime_state_ops.h"
#include "desktoper2D/lifecycle/model_reload_service.h"
#include "desktoper2D/lifecycle/behavior_applier.h"
#include "desktoper2D/lifecycle/bridge_factory.h"
#include "desktoper2D/lifecycle/editor/editor_interaction_facade.h"
#include "desktoper2D/lifecycle/editor/editor_session_service.h"
#include "desktoper2D/lifecycle/legacy_editor_ui_tools.h"
#include "desktoper2D/lifecycle/services/task_category_service.h"
#include "desktoper2D/controllers/interaction_controller.h"

namespace desktoper2D {
namespace {

void SetClickThrough(AppRuntime &runtime, bool enabled) {
    desktoper2D::SetClickThrough(runtime, enabled);
}

}  // namespace

ModelReloadServiceContext BuildModelReloadServiceContext(AppRuntime &runtime) {
    ModelReloadServiceContext reload_ctx{};
    reload_ctx.dev_hot_reload_enabled = runtime.dev_hot_reload_enabled;
    reload_ctx.hot_reload_poll_accum_sec = &runtime.hot_reload_poll_accum_sec;
    reload_ctx.model_loaded = &runtime.model_loaded;
    reload_ctx.model = &runtime.model;
    reload_ctx.renderer = runtime.window_state.renderer;
    reload_ctx.model_time = &runtime.model_time;
    reload_ctx.selected_part_index = &runtime.selected_part_index;
    reload_ctx.model_last_write_time = &runtime.model_last_write_time;
    reload_ctx.model_last_write_time_valid = &runtime.model_last_write_time_valid;
    reload_ctx.ensure_selected_part_index_valid = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) desktoper2D::EnsureSelectedPartIndexValid(*rt);
    };
    reload_ctx.ensure_selected_part_index_valid_user_data = &runtime;
    reload_ctx.ensure_selected_param_index_valid = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) desktoper2D::EnsureSelectedParamIndexValid(*rt);
    };
    reload_ctx.ensure_selected_param_index_valid_user_data = &runtime;
    reload_ctx.sync_animation_channel_state = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) desktoper2D::SyncAnimationChannelState(*rt);
    };
    reload_ctx.sync_animation_channel_state_user_data = &runtime;
    reload_ctx.set_editor_status = [](const std::string &text, float ttl_sec, void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) desktoper2D::SetEditorStatus(*rt, text, ttl_sec);
    };
    reload_ctx.set_editor_status_user_data = &runtime;
    return reload_ctx;
}

BehaviorApplyContext BuildBehaviorApplyContext(AppRuntime &runtime) {
    BehaviorApplyContext apply_ctx{};
    apply_ctx.model_loaded = runtime.model_loaded;
    apply_ctx.model = &runtime.model;
    apply_ctx.plugin_param_blend_mode = runtime.plugin.param_blend_mode;
    apply_ctx.window = runtime.window_state.window;
    apply_ctx.show_debug_stats = &runtime.show_debug_stats;
    apply_ctx.manual_param_mode = &runtime.manual_param_mode;
    apply_ctx.set_click_through = [](bool enabled, void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) desktoper2D::SetClickThrough(*rt, enabled);
    };
    apply_ctx.set_click_through_user_data = &runtime;
    apply_ctx.sync_animation_channel_state = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) desktoper2D::SyncAnimationChannelState(*rt);
    };
    apply_ctx.sync_animation_channel_state_user_data = &runtime;
    return apply_ctx;
}

EditorInputBindingBridge BuildEditorInputBindingBridge(AppRuntime &runtime) {
    const EditorInputBindingFactoryDeps deps{
        .ensure_selected_part_index_valid = [&runtime]() { EnsureSelectedPartIndexValid(runtime); },
        .cycle_selected_part = [&runtime](bool shift) { CycleSelectedParam(runtime, shift ? -1 : 1); },
        .adjust_selected_param = [&runtime](float delta) { AdjustSelectedParam(runtime, delta); },
        .reset_selected_param = [&runtime]() { ResetSelectedParam(runtime); },
        .reset_all_params = [&runtime]() { ResetAllParams(runtime); },
        .toggle_edit_mode = [&runtime]() {
            runtime.edit_mode = !runtime.edit_mode;
            if (runtime.edit_mode) {
                desktoper2D::EnsureSelectedPartIndexValid(runtime);
                desktoper2D::SetEditorStatus(runtime, "edit mode ON", 1.5f);
            } else {
                EndDragging(runtime, runtime.editor_controller_state);
                EndGizmoDrag(runtime, runtime.editor_controller_state);
                runtime.gizmo_hover_handle = GizmoHandle::None;
                desktoper2D::SetEditorStatus(runtime, "edit mode OFF", 1.5f);
            }
        },
        .toggle_manual_param_mode = [&runtime]() { ToggleManualParamMode(runtime); },
        .save_model = [&runtime]() { SaveEditedModelJsonToDisk(runtime); },
        .save_project = [&runtime]() { SaveEditorProjectToDisk(runtime); },
        .save_project_as = [&runtime]() { SaveEditorProjectAsToDisk(runtime); },
        .load_project = [&runtime]() { LoadEditorProjectFromDisk(runtime); },
        .undo_edit = [&runtime]() { UndoLastEdit(runtime); },
        .redo_edit = [&runtime]() { RedoLastEdit(runtime); },
        .pick_top_part_at = [&runtime](float x, float y) { return desktoper2D::PickTopPartAt(runtime, x, y); },
        .has_model_params = [&runtime]() { return HasModelParams(runtime); },
        .on_head_pat_mouse_motion = [&runtime](float mx, float my) {
            HandleHeadPatMouseMotion(
                runtime.interaction_state,
                InteractionControllerContext{
                    .model_loaded = runtime.model_loaded,
                    .model = &runtime.model,
                    .pick_top_part_at = [&runtime](float x, float y) { return desktoper2D::PickTopPartAt(runtime, x, y); },
                    .has_model_params = [&runtime]() { return HasModelParams(runtime); },
                },
                mx,
                my);
        },
        .on_head_pat_mouse_down = [&runtime](float mx, float my) {
            HandleHeadPatMouseDown(
                runtime.interaction_state,
                InteractionControllerContext{
                    .model_loaded = runtime.model_loaded,
                    .model = &runtime.model,
                    .pick_top_part_at = [&runtime](float x, float y) { return desktoper2D::PickTopPartAt(runtime, x, y); },
                    .has_model_params = [&runtime]() { return HasModelParams(runtime); },
                },
                mx,
                my);
        },
        .begin_drag_part = [&runtime](float mx, float my) {
            BeginDragPart(runtime, runtime.editor_controller_state, mx, my);
        },
        .begin_drag_pivot = [&runtime](float mx, float my) {
            BeginDragPivot(runtime, runtime.editor_controller_state, mx, my);
        },
        .end_dragging = [&runtime]() { EndDragging(runtime, runtime.editor_controller_state); },
        .begin_gizmo_drag = [&runtime](const ModelPart &part, GizmoHandle handle, float mx, float my) {
            BeginGizmoDrag(runtime, runtime.editor_controller_state, part, handle, mx, my);
        },
        .end_gizmo_drag = [&runtime]() { EndGizmoDrag(runtime, runtime.editor_controller_state); },
        .handle_gizmo_drag_motion = [&runtime](float mx, float my) {
            HandleGizmoDragMotion(runtime, runtime.editor_controller_state, mx, my);
        },
        .handle_editor_drag_motion = [&runtime](float mx, float my) {
            HandleEditorDragMotion(runtime, runtime.editor_controller_state, mx, my);
        },
    };
    return desktoper2D::BuildEditorInputBindingBridge(deps);
}

AppEventHandlerBridge BuildAppEventHandlerBridge(AppRuntime &runtime) {
    const AppEventBridgeFactoryDeps deps{
        .build_editor_input_callbacks = [&runtime]() {
            return BuildEditorInputCallbacksFromRuntime(runtime, BuildEditorInputBindingBridge(runtime));
        },
        .toggle_edit_mode = [&runtime]() {
            runtime.edit_mode = !runtime.edit_mode;
            if (runtime.edit_mode) {
                desktoper2D::EnsureSelectedPartIndexValid(runtime);
                desktoper2D::SetEditorStatus(runtime, "edit mode ON", 1.5f);
            } else {
                EndDragging(runtime, runtime.editor_controller_state);
                EndGizmoDrag(runtime, runtime.editor_controller_state);
                runtime.gizmo_hover_handle = GizmoHandle::None;
                desktoper2D::SetEditorStatus(runtime, "edit mode OFF", 1.5f);
            }
        },
        .toggle_manual_param_mode = [&runtime]() { ToggleManualParamMode(runtime); },
        .cycle_selected_part = [&runtime](bool shift) { CycleSelectedParam(runtime, shift ? -1 : 1); },
        .adjust_selected_param = [&runtime](float delta) { AdjustSelectedParam(runtime, delta); },
        .reset_selected_param = [&runtime]() { ResetSelectedParam(runtime); },
        .reset_all_params = [&runtime]() { ResetAllParams(runtime); },
        .on_head_pat_mouse_motion = [&runtime](float mx, float my) {
            HandleHeadPatMouseMotion(
                runtime.interaction_state,
                InteractionControllerContext{
                    .model_loaded = runtime.model_loaded,
                    .model = &runtime.model,
                    .pick_top_part_at = [&runtime](float x, float y) { return desktoper2D::PickTopPartAt(runtime, x, y); },
                    .has_model_params = [&runtime]() { return HasModelParams(runtime); },
                },
                mx,
                my);
        },
        .on_head_pat_mouse_down = [&runtime](float mx, float my) {
            HandleHeadPatMouseDown(
                runtime.interaction_state,
                InteractionControllerContext{
                    .model_loaded = runtime.model_loaded,
                    .model = &runtime.model,
                    .pick_top_part_at = [&runtime](float x, float y) { return desktoper2D::PickTopPartAt(runtime, x, y); },
                    .has_model_params = [&runtime]() { return HasModelParams(runtime); },
                },
                mx,
                my);
        },
    };
    return desktoper2D::BuildAppEventHandlerBridge(deps);
}

RuntimeTickBridge BuildRuntimeTickBridge(AppRuntime &runtime) {
    const RuntimeTickBridgeFactoryDeps deps{
        .pick_top_part_at = [&runtime](float x, float y) { return desktoper2D::PickTopPartAt(runtime, x, y); },
        .has_model_params = [&runtime]() { return HasModelParams(runtime); },
        .build_model_reload_context = [&runtime]() { return BuildModelReloadServiceContext(runtime); },
        .build_behavior_apply_context = [&runtime]() { return BuildBehaviorApplyContext(runtime); },
        .task_secondary_category_name = [](TaskSecondaryCategory c) { return TaskSecondaryCategoryName(c); },
    };
    return desktoper2D::BuildRuntimeTickBridge(deps);
}

RuntimeRenderBridge BuildRuntimeRenderBridge(AppRuntime &runtime) {
    const RuntimeRenderBridgeFactoryDeps deps{
        .has_model_parts = [&runtime]() { return HasModelParts(runtime); },
        .has_model_params = [&runtime]() { return HasModelParams(runtime); },
        .ensure_selected_part_index_valid = [&runtime]() { desktoper2D::EnsureSelectedPartIndexValid(runtime); },
        .ensure_selected_param_index_valid = [&runtime]() { desktoper2D::EnsureSelectedParamIndexValid(runtime); },
        .compute_part_aabb = [](const ModelPart &part, SDL_FRect *out_rect) {
            return desktoper2D::ComputePartAABB(part, out_rect);
        },
        .render_model_hierarchy_tree = [&runtime]() {
            desktoper2D::RenderModelHierarchyTree(runtime.model,
                                          &runtime.selected_part_index,
                                          runtime.resource_tree_filter,
                                          runtime.resource_tree_auto_expand_matches);
        },
        .render_resource_tree_inspector = [&runtime]() {
            desktoper2D::RenderResourceTreeInspector(runtime,
                                             runtime.model,
                                             &runtime.selected_part_index,
                                             &runtime.selected_deformer_type,
                                             runtime.resource_tree_filter,
                                             static_cast<int>(sizeof(runtime.resource_tree_filter)),
                                             &runtime.resource_tree_auto_expand_matches,
                                             [&runtime](int idx) {
                                                 runtime.selected_part_index = idx;
                                             });
        },
        .task_primary_category_name = [&runtime]() { return TaskPrimaryCategoryName(runtime.task_decision.primary); },
        .task_secondary_category_name = [&runtime]() { return TaskSecondaryCategoryName(runtime.task_decision.secondary); },
    };
    return desktoper2D::BuildRuntimeRenderBridge(deps);
}

EditorInputCallbacks BuildEditorInputCallbacks(AppRuntime &runtime) {
    return BuildEditorInputCallbacksFromRuntime(runtime, BuildEditorInputBindingBridge(runtime));
}

}  // namespace desktoper2D
