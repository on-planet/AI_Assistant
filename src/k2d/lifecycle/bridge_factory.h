#pragma once

#include <SDL3/SDL.h>

#include "k2d/editor/editor_input.h"
#include "k2d/editor/editor_input_binding.h"
#include "k2d/lifecycle/events/app_event_handler.h"
#include "k2d/lifecycle/systems/runtime_tick_entry.h"
#include "k2d/lifecycle/ui/runtime_render_entry.h"

#include <functional>

namespace k2d {

struct AppRuntime;
struct BehaviorApplyContext;
struct ModelPart;
struct ModelReloadServiceContext;

template <typename T>
using RuntimeFn = std::function<T(AppRuntime &)>;

struct EditorInputBindingFactoryDeps {
    std::function<void()> ensure_selected_part_index_valid;
    std::function<void(bool)> cycle_selected_part;
    std::function<void(float)> adjust_selected_param;
    std::function<void()> reset_selected_param;
    std::function<void()> reset_all_params;

    std::function<void(AppRuntime &)> toggle_edit_mode;
    std::function<void()> toggle_manual_param_mode;
    std::function<void()> save_model;
    std::function<void()> undo_edit;
    std::function<void()> redo_edit;

    std::function<int(AppRuntime &, float, float)> pick_top_part_at;
    std::function<bool()> has_model_params;
    std::function<void(AppRuntime &, float, float)> on_head_pat_mouse_motion;
    std::function<void(AppRuntime &, float, float)> on_head_pat_mouse_down;

    std::function<void(float, float)> begin_drag_part;
    std::function<void(float, float)> begin_drag_pivot;
    std::function<void()> end_dragging;
    std::function<void(const ModelPart &, GizmoHandle, float, float)> begin_gizmo_drag;
    std::function<void()> end_gizmo_drag;
    std::function<void(float, float)> handle_gizmo_drag_motion;
    std::function<void(float, float)> handle_editor_drag_motion;
};

EditorInputBindingBridge BuildEditorInputBindingBridge(AppRuntime &runtime,
                                                       const EditorInputBindingFactoryDeps &deps);

struct AppEventBridgeFactoryDeps {
    RuntimeFn<EditorInputCallbacks> build_editor_input_callbacks;

    std::function<void(AppRuntime &)> toggle_edit_mode;
    std::function<void()> toggle_manual_param_mode;
    std::function<void(bool)> cycle_selected_part;
    std::function<void(float)> adjust_selected_param;
    std::function<void()> reset_selected_param;
    std::function<void()> reset_all_params;

    std::function<void(AppRuntime &, float, float)> on_head_pat_mouse_motion;
    std::function<void(AppRuntime &, float, float)> on_head_pat_mouse_down;
};

AppEventHandlerBridge BuildAppEventHandlerBridge(AppRuntime &runtime,
                                                 const AppEventBridgeFactoryDeps &deps);

struct RuntimeTickBridgeFactoryDeps {
    std::function<int(AppRuntime &, float, float)> pick_top_part_at;
    std::function<bool()> has_model_params;
    RuntimeFn<ModelReloadServiceContext> build_model_reload_context;
    RuntimeFn<BehaviorApplyContext> build_behavior_apply_context;
    std::function<const char *(TaskSecondaryCategory)> task_secondary_category_name;
    std::function<void(AppRuntime &)> infer_task_category_inplace;
};

RuntimeTickBridge BuildRuntimeTickBridge(AppRuntime &runtime,
                                         const RuntimeTickBridgeFactoryDeps &deps);

struct RuntimeRenderBridgeFactoryDeps {
    std::function<bool()> has_model_parts;
    std::function<bool()> has_model_params;
    std::function<void(AppRuntime &)> ensure_selected_part_index_valid;
    std::function<void(AppRuntime &)> ensure_selected_param_index_valid;
    std::function<bool(const ModelPart &, SDL_FRect *)> compute_part_aabb;
    std::function<void(AppRuntime &)> render_model_hierarchy_tree;
    std::function<const char *(AppRuntime &)> task_primary_category_name;
    std::function<const char *(AppRuntime &)> task_secondary_category_name;
};

RuntimeRenderBridge BuildRuntimeRenderBridge(AppRuntime &runtime,
                                             const RuntimeRenderBridgeFactoryDeps &deps);

}  // namespace k2d
