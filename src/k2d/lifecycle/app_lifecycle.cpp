#include "k2d/lifecycle/app_lifecycle.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_tray.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "k2d/core/model.h"
#include "k2d/core/png_texture.h"
#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/inference_adapter.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/editor/editor_gizmo.h"
#include "k2d/editor/editor_input.h"
#include "k2d/editor/editor_controller.h"
#include "k2d/editor/editor_input_binding.h"
#include "k2d/rendering/app_renderer.h"
#include "k2d/controllers/app_bootstrap.h"
#include "k2d/controllers/window_controller.h"
#include "k2d/controllers/param_controller.h"
#include "k2d/controllers/app_loop.h"
#include "k2d/controllers/app_input_controller.h"
#include "k2d/controllers/interaction_controller.h"
#include "k2d/lifecycle/plugin_lifecycle.h"
#include "k2d/lifecycle/behavior_applier.h"
#include "k2d/lifecycle/model_reload_service.h"
#include "k2d/lifecycle/reminder_service.h"
#include "k2d/lifecycle/perception_pipeline.h"
#include "k2d/lifecycle/systems/app_systems.h"
#include "k2d/lifecycle/systems/runtime_tick_entry.h"
#include "k2d/lifecycle/events/app_event_handler.h"
#include "k2d/lifecycle/ui/app_debug_ui.h"
#include "k2d/lifecycle/ui/reminder_panel.h"
#include "k2d/lifecycle/ui/runtime_render_entry.h"
#include "k2d/lifecycle/editor/editor_interaction_facade.h"
#include "k2d/lifecycle/editor/editor_session_service.h"
#include "k2d/lifecycle/bridge_factory.h"
#include "k2d/lifecycle/utils/app_text_utils.h"
#include "k2d/lifecycle/state/runtime_state_ops.h"
#include "k2d/lifecycle/services/task_category_service.h"
#include "k2d/lifecycle/asr/cloud_asr_provider.h"
#include "k2d/lifecycle/asr/hybrid_asr_provider.h"
#include "k2d/lifecycle/asr/offline_asr_provider.h"
#include "k2d/lifecycle/legacy_editor_ui_tools.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <limits>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace k2d {

namespace {


void EnsureSelectedPartIndexValid();
void EnsureSelectedPartIndexValid(AppRuntime &runtime);
void EnsureSelectedParamIndexValid();
void EnsureSelectedParamIndexValid(AppRuntime &runtime);
void SyncAnimationChannelState();
void SyncAnimationChannelState(AppRuntime &runtime);
void SetEditorStatus(std::string text, float ttl_sec = 2.0f);
void SetEditorStatus(AppRuntime &runtime, std::string text, float ttl_sec = 2.0f);
void SetClickThrough(bool enabled);
void SetClickThrough(AppRuntime &runtime, bool enabled);

void SDLCALL MicAudioInputCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    (void)total_amount;
    auto *runtime = static_cast<AppRuntime *>(userdata);
    if (!runtime || additional_amount <= 0) {
        return;
    }

    std::vector<float> tmp(static_cast<std::size_t>(additional_amount / static_cast<int>(sizeof(float))));
    const int got = SDL_GetAudioStreamData(stream, tmp.data(), additional_amount);
    if (got <= 0) {
        return;
    }

    const int n = got / static_cast<int>(sizeof(float));
    std::lock_guard<std::mutex> lk(runtime->mic_mutex);
    for (int i = 0; i < n; ++i) {
        runtime->mic_pcm_queue.push_back(tmp[static_cast<std::size_t>(i)]);
    }
    constexpr std::size_t kMaxQueueSamples = 16000 * 20;
    while (runtime->mic_pcm_queue.size() > kMaxQueueSamples) {
        runtime->mic_pcm_queue.pop_front();
    }
}

ModelReloadServiceContext BuildModelReloadServiceContext(AppRuntime &runtime) {
    ModelReloadServiceContext reload_ctx{};
    reload_ctx.dev_hot_reload_enabled = runtime.dev_hot_reload_enabled;
    reload_ctx.hot_reload_poll_accum_sec = &runtime.hot_reload_poll_accum_sec;
    reload_ctx.model_loaded = &runtime.model_loaded;
    reload_ctx.model = &runtime.model;
    reload_ctx.renderer = runtime.renderer;
    reload_ctx.model_time = &runtime.model_time;
    reload_ctx.selected_part_index = &runtime.selected_part_index;
    reload_ctx.model_last_write_time = &runtime.model_last_write_time;
    reload_ctx.model_last_write_time_valid = &runtime.model_last_write_time_valid;
    reload_ctx.ensure_selected_part_index_valid = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) k2d::EnsureSelectedPartIndexValid(*rt);
    };
    reload_ctx.ensure_selected_part_index_valid_user_data = &runtime;
    reload_ctx.ensure_selected_param_index_valid = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) k2d::EnsureSelectedParamIndexValid(*rt);
    };
    reload_ctx.ensure_selected_param_index_valid_user_data = &runtime;
    reload_ctx.sync_animation_channel_state = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) k2d::SyncAnimationChannelState(*rt);
    };
    reload_ctx.sync_animation_channel_state_user_data = &runtime;
    reload_ctx.set_editor_status = [](const std::string &text, float ttl_sec, void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) k2d::SetEditorStatus(*rt, text, ttl_sec);
    };
    reload_ctx.set_editor_status_user_data = &runtime;
    return reload_ctx;
}

BehaviorApplyContext BuildBehaviorApplyContext(AppRuntime &runtime) {
    BehaviorApplyContext apply_ctx{};
    apply_ctx.model_loaded = runtime.model_loaded;
    apply_ctx.model = &runtime.model;
    apply_ctx.plugin_param_blend_mode = runtime.plugin_param_blend_mode;
    apply_ctx.window = runtime.window;
    apply_ctx.show_debug_stats = &runtime.show_debug_stats;
    apply_ctx.manual_param_mode = &runtime.manual_param_mode;
    apply_ctx.set_click_through = [](bool enabled, void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) k2d::SetClickThrough(*rt, enabled);
    };
    apply_ctx.set_click_through_user_data = &runtime;
    apply_ctx.sync_animation_channel_state = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) k2d::SyncAnimationChannelState(*rt);
    };
    apply_ctx.sync_animation_channel_state_user_data = &runtime;
    return apply_ctx;
}

void SetClickThrough(bool enabled) {
    k2d::SetClickThrough(g_runtime, enabled);
}

void SetClickThrough(AppRuntime &runtime, bool enabled) {
    k2d::SetClickThrough(runtime, enabled);
}

void ToggleWindowVisibility() {
    k2d::ToggleWindowVisibility(g_runtime);
}

void SDLCALL TrayToggleClickThrough(void *userdata, SDL_TrayEntry *entry) {
    (void)userdata;
    (void)entry;
    SetClickThrough(!g_runtime.click_through);
}

void SDLCALL TrayToggleVisibility(void *userdata, SDL_TrayEntry *entry) {
    (void)userdata;
    (void)entry;
    ToggleWindowVisibility();
}

void SDLCALL TrayQuit(void *userdata, SDL_TrayEntry *entry) {
    (void)userdata;
    (void)entry;
    g_runtime.running = false;
}

SDL_HitTestResult SDLCALL WindowHitTest(SDL_Window *window, const SDL_Point *area, void *data) {
    (void)window;
    (void)data;
    return k2d::WindowHitTest(g_runtime.click_through, g_runtime.edit_mode, g_runtime.interactive_rect, area);
}

k2d::ParamControllerContext BuildParamControllerContext() {
    return k2d::BuildParamControllerContext(g_runtime);
}

void SyncAnimationChannelState() {
    k2d::SyncAnimationChannelState(g_runtime);
}

void SyncAnimationChannelState(AppRuntime &runtime) {
    k2d::SyncAnimationChannelState(runtime);
}

bool HasModelParams() {
    return k2d::HasModelParams(g_runtime);
}

void EnsureSelectedParamIndexValid() {
    k2d::EnsureSelectedParamIndexValid(g_runtime);
}

void EnsureSelectedParamIndexValid(AppRuntime &runtime) {
    k2d::EnsureSelectedParamIndexValid(runtime);
}

void CycleSelectedParam(int delta) {
    k2d::CycleSelectedParam(g_runtime, delta);
}

void AdjustSelectedParam(float delta) {
    k2d::AdjustSelectedParam(g_runtime, delta);
}

void ResetSelectedParam() {
    k2d::ResetSelectedParam(g_runtime);
}

void ResetAllParams() {
    k2d::ResetAllParams(g_runtime);
}

void ToggleManualParamMode() {
    k2d::ToggleManualParamMode(g_runtime);
}

bool HasModelParts() {
    return k2d::HasModelParts(g_runtime);
}

void SetEditorStatus(std::string text, float ttl_sec) {
    k2d::SetEditorStatus(g_runtime, std::move(text), ttl_sec);
}

void SetEditorStatus(AppRuntime &runtime, std::string text, float ttl_sec) {
    k2d::SetEditorStatus(runtime, std::move(text), ttl_sec);
}

float QuantizeToGrid(float v, float grid) {
    return k2d::QuantizeToGrid(v, grid);
}

const char *AxisConstraintName(AxisConstraint c) {
    return k2d::AxisConstraintName(c);
}


const char *EditorPropName(EditorProp p) {
    return k2d::EditorPropName(p);
}

void RenderModelHierarchyTree(ModelRuntime &model, int selected_part_index) {
    k2d::RenderModelHierarchyTree(model, selected_part_index);
}

void UndoLastEdit() {
    k2d::UndoLastEdit(g_runtime);
}

void RedoLastEdit() {
    k2d::RedoLastEdit(g_runtime);
}

void CycleSelectedEditorProp(int delta) {
    int n = static_cast<int>(EditorProp::Count);
    int next = g_runtime.selected_editor_prop + delta;
    next %= n;
    if (next < 0) {
        next += n;
    }
    g_runtime.selected_editor_prop = next;
}

void AdjustSelectedEditorProp(float delta) {
    if (!HasModelParts()) {
        return;
    }

    EnsureSelectedPartIndexValid();
    if (g_runtime.selected_part_index < 0 ||
        g_runtime.selected_part_index >= static_cast<int>(g_runtime.model.parts.size())) {
        return;
    }

    ModelPart &part = g_runtime.model.parts[static_cast<std::size_t>(g_runtime.selected_part_index)];
    const EditorProp prop = static_cast<EditorProp>(g_runtime.selected_editor_prop);

    switch (prop) {
        case EditorProp::PosX:
            part.base_pos_x += delta;
            break;
        case EditorProp::PosY:
            part.base_pos_y += delta;
            break;
        case EditorProp::PivotX:
            ApplyPivotDelta(&part, delta, 0.0f);
            break;
        case EditorProp::PivotY:
            ApplyPivotDelta(&part, 0.0f, delta);
            break;
        case EditorProp::RotDeg:
            part.base_rot_deg += delta;
            break;
        case EditorProp::ScaleX:
            part.base_scale_x = std::max(0.05f, part.base_scale_x + delta);
            break;
        case EditorProp::ScaleY:
            part.base_scale_y = std::max(0.05f, part.base_scale_y + delta);
            break;
        default:
            break;
    }
}

void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y) {
    k2d::ApplyPivotDelta(part, delta_x, delta_y);
}

void EnsureSelectedPartIndexValid() {
    k2d::EnsureSelectedPartIndexValid(g_runtime);
}

void EnsureSelectedPartIndexValid(AppRuntime &runtime) {
    k2d::EnsureSelectedPartIndexValid(runtime);
}

void CycleSelectedPart(int delta) {
    if (!HasModelParts()) {
        g_runtime.selected_part_index = -1;
        return;
    }
    EnsureSelectedPartIndexValid();
    const int count = static_cast<int>(g_runtime.model.parts.size());
    int next = g_runtime.selected_part_index + delta;
    next %= count;
    if (next < 0) {
        next += count;
    }
    g_runtime.selected_part_index = next;
}

bool ComputePartAABB(const ModelPart &part, SDL_FRect *out_rect) {
    return k2d::ComputePartAABB(part, out_rect);
}

int PickTopPartAt(float x, float y) {
    return k2d::PickTopPartAt(g_runtime, x, y);
}

void BeginDragPart(float mouse_x, float mouse_y) {
    k2d::BeginDragPart(g_runtime, g_editor_state, mouse_x, mouse_y);
}

void BeginDragPivot(float mouse_x, float mouse_y) {
    k2d::BeginDragPivot(g_runtime, g_editor_state, mouse_x, mouse_y);
}

void EndDragging() {
    k2d::EndDragging(g_runtime, g_editor_state);
}

void BeginGizmoDrag(const ModelPart &part, GizmoHandle handle, float mouse_x, float mouse_y) {
    k2d::BeginGizmoDrag(g_runtime, g_editor_state, part, handle, mouse_x, mouse_y);
}

void EndGizmoDrag() {
    k2d::EndGizmoDrag(g_runtime, g_editor_state);
}

void HandleGizmoDragMotion(float mouse_x, float mouse_y) {
    k2d::HandleGizmoDragMotion(g_runtime, g_editor_state, mouse_x, mouse_y);
}

void HandleEditorDragMotion(float mouse_x, float mouse_y) {
    k2d::HandleEditorDragMotion(g_runtime, g_editor_state, mouse_x, mouse_y);
}

void SaveEditedModelJsonToDisk() {
    k2d::SaveEditedModelJsonToDisk(g_runtime);
}



EditorInputBindingBridge BuildEditorInputBindingBridge(AppRuntime &runtime) {
    const EditorInputBindingFactoryDeps deps{
        .ensure_selected_part_index_valid = []() { EnsureSelectedPartIndexValid(); },
        .cycle_selected_part = [](bool shift) { CycleSelectedPart(shift ? -1 : 1); },
        .adjust_selected_param = [](float delta) { AdjustSelectedParam(delta); },
        .reset_selected_param = []() { ResetSelectedParam(); },
        .reset_all_params = []() { ResetAllParams(); },
        .toggle_edit_mode = [](AppRuntime &rt) {
            rt.edit_mode = !rt.edit_mode;
            if (rt.edit_mode) {
                k2d::EnsureSelectedPartIndexValid(rt);
                k2d::SetEditorStatus(rt, "edit mode ON", 1.5f);
            } else {
                EndDragging();
                EndGizmoDrag();
                rt.gizmo_hover_handle = GizmoHandle::None;
                k2d::SetEditorStatus(rt, "edit mode OFF", 1.5f);
            }
        },
        .toggle_manual_param_mode = []() { ToggleManualParamMode(); },
        .save_model = []() { SaveEditedModelJsonToDisk(); },
        .undo_edit = []() { UndoLastEdit(); },
        .redo_edit = []() { RedoLastEdit(); },
        .pick_top_part_at = [](AppRuntime &rt, float x, float y) { return k2d::PickTopPartAt(rt, x, y); },
        .has_model_params = []() { return HasModelParams(); },
        .on_head_pat_mouse_motion = [](AppRuntime &rt, float mx, float my) {
            HandleHeadPatMouseMotion(
                rt.interaction_state,
                InteractionControllerContext{
                    .model_loaded = rt.model_loaded,
                    .model = &rt.model,
                    .pick_top_part_at = [&rt](float x, float y) { return k2d::PickTopPartAt(rt, x, y); },
                    .has_model_params = []() { return HasModelParams(); },
                },
                mx,
                my);
        },
        .on_head_pat_mouse_down = [](AppRuntime &rt, float mx, float my) {
            HandleHeadPatMouseDown(
                rt.interaction_state,
                InteractionControllerContext{
                    .model_loaded = rt.model_loaded,
                    .model = &rt.model,
                    .pick_top_part_at = [&rt](float x, float y) { return k2d::PickTopPartAt(rt, x, y); },
                    .has_model_params = []() { return HasModelParams(); },
                },
                mx,
                my);
        },
        .begin_drag_part = [](float mx, float my) { BeginDragPart(mx, my); },
        .begin_drag_pivot = [](float mx, float my) { BeginDragPivot(mx, my); },
        .end_dragging = []() { EndDragging(); },
        .begin_gizmo_drag = [](const ModelPart &part, GizmoHandle handle, float mx, float my) {
            BeginGizmoDrag(part, handle, mx, my);
        },
        .end_gizmo_drag = []() { EndGizmoDrag(); },
        .handle_gizmo_drag_motion = [](float mx, float my) { HandleGizmoDragMotion(mx, my); },
        .handle_editor_drag_motion = [](float mx, float my) { HandleEditorDragMotion(mx, my); },
    };
    return k2d::BuildEditorInputBindingBridge(runtime, deps);
}

AppEventHandlerBridge BuildAppEventHandlerBridge(AppRuntime &runtime) {
    const AppEventBridgeFactoryDeps deps{
        .build_editor_input_callbacks = [](AppRuntime &rt) {
            return BuildEditorInputCallbacksFromRuntime(rt, BuildEditorInputBindingBridge(rt));
        },
        .toggle_edit_mode = [](AppRuntime &rt) {
            rt.edit_mode = !rt.edit_mode;
            if (rt.edit_mode) {
                k2d::EnsureSelectedPartIndexValid(rt);
                k2d::SetEditorStatus(rt, "edit mode ON", 1.5f);
            } else {
                EndDragging();
                EndGizmoDrag();
                rt.gizmo_hover_handle = GizmoHandle::None;
                k2d::SetEditorStatus(rt, "edit mode OFF", 1.5f);
            }
        },
        .toggle_manual_param_mode = []() { ToggleManualParamMode(); },
        .cycle_selected_part = [](bool shift) { CycleSelectedPart(shift ? -1 : 1); },
        .adjust_selected_param = [](float delta) { AdjustSelectedParam(delta); },
        .reset_selected_param = []() { ResetSelectedParam(); },
        .reset_all_params = []() { ResetAllParams(); },
        .on_head_pat_mouse_motion = [](AppRuntime &rt, float mx, float my) {
            HandleHeadPatMouseMotion(
                rt.interaction_state,
                InteractionControllerContext{
                    .model_loaded = rt.model_loaded,
                    .model = &rt.model,
                    .pick_top_part_at = [&rt](float x, float y) { return k2d::PickTopPartAt(rt, x, y); },
                    .has_model_params = []() { return HasModelParams(); },
                },
                mx,
                my);
        },
        .on_head_pat_mouse_down = [](AppRuntime &rt, float mx, float my) {
            HandleHeadPatMouseDown(
                rt.interaction_state,
                InteractionControllerContext{
                    .model_loaded = rt.model_loaded,
                    .model = &rt.model,
                    .pick_top_part_at = [&rt](float x, float y) { return k2d::PickTopPartAt(rt, x, y); },
                    .has_model_params = []() { return HasModelParams(); },
                },
                mx,
                my);
        },
    };
    return k2d::BuildAppEventHandlerBridge(runtime, deps);
}

RuntimeTickBridge BuildRuntimeTickBridge(AppRuntime &runtime) {
    const RuntimeTickBridgeFactoryDeps deps{
        .pick_top_part_at = [](AppRuntime &rt, float x, float y) { return k2d::PickTopPartAt(rt, x, y); },
        .has_model_params = []() { return HasModelParams(); },
        .build_model_reload_context = [](AppRuntime &rt) { return BuildModelReloadServiceContext(rt); },
        .build_behavior_apply_context = [](AppRuntime &rt) { return BuildBehaviorApplyContext(rt); },
        .task_secondary_category_name = [](TaskSecondaryCategory c) { return TaskSecondaryCategoryName(c); },
        .infer_task_category_inplace = [](AppRuntime &rt) {
            InferTaskCategory(rt.perception_state.system_context_snapshot,
                              rt.perception_state.ocr_result,
                              rt.perception_state.scene_result,
                              rt.task_category_config,
                              rt.task_primary,
                              rt.task_secondary);
        },
    };
    return k2d::BuildRuntimeTickBridge(runtime, deps);
}

RuntimeRenderBridge BuildRuntimeRenderBridge(AppRuntime &runtime) {
    const RuntimeRenderBridgeFactoryDeps deps{
        .has_model_parts = []() { return HasModelParts(); },
        .has_model_params = []() { return HasModelParams(); },
        .ensure_selected_part_index_valid = [](AppRuntime &rt) { k2d::EnsureSelectedPartIndexValid(rt); },
        .ensure_selected_param_index_valid = [](AppRuntime &rt) { k2d::EnsureSelectedParamIndexValid(rt); },
        .compute_part_aabb = [](const ModelPart &part, SDL_FRect *out_rect) {
            return k2d::ComputePartAABB(part, out_rect);
        },
        .render_model_hierarchy_tree = [](AppRuntime &rt) {
            k2d::RenderModelHierarchyTree(rt.model, rt.selected_part_index);
        },
        .task_primary_category_name = [](AppRuntime &rt) { return TaskPrimaryCategoryName(rt.task_primary); },
        .task_secondary_category_name = [](AppRuntime &rt) { return TaskSecondaryCategoryName(rt.task_secondary); },
    };
    return k2d::BuildRuntimeRenderBridge(runtime, deps);
}

}  // namespace

k2d::EditorInputCallbacks BuildEditorInputCallbacks(AppRuntime &runtime) {
    return BuildEditorInputCallbacksFromRuntime(runtime, BuildEditorInputBindingBridge(runtime));
}

void AppLifecycleRun(AppLifecycleContext &ctx) {
    AppRuntime &runtime = ctx.runtime ? *ctx.runtime : g_runtime;
    k2d::RunAppLoop(k2d::AppLoopContext{
        .running = &runtime.running,
        .window_visible = &runtime.window_visible,
        .model_time = &runtime.model_time,
        .editor_status_ttl = &runtime.editor_status_ttl,
        .debug_frame_ms = &runtime.debug_frame_ms,
        .debug_fps = &runtime.debug_fps,
        .debug_fps_accum_sec = &runtime.debug_fps_accum_sec,
        .debug_fps_accum_frames = &runtime.debug_fps_accum_frames,
        .handle_event = [&runtime](const SDL_Event &event) {
            HandleAppRuntimeEvent(runtime, event, BuildAppEventHandlerBridge(runtime));
        },
        .on_update = [&runtime](float dt) {
            RunRuntimeTickEntry(runtime, dt, BuildRuntimeTickBridge(runtime));
        },
        .on_render = [&runtime]() {
            RunRuntimeRenderEntry(runtime, BuildRuntimeRenderBridge(runtime));
        },
        .on_editor_status_expired = [&runtime]() {
            runtime.editor_status.clear();
        },
    });
}

}  // namespace k2d



