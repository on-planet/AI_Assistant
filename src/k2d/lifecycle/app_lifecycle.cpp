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
#include "k2d/lifecycle/utils/app_text_utils.h"
#include "k2d/lifecycle/services/task_category_service.h"
#include "k2d/lifecycle/asr/cloud_asr_provider.h"
#include "k2d/lifecycle/asr/hybrid_asr_provider.h"
#include "k2d/lifecycle/asr/offline_asr_provider.h"

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


void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y);
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
        if (rt) EnsureSelectedPartIndexValid(*rt);
    };
    reload_ctx.ensure_selected_part_index_valid_user_data = &runtime;
    reload_ctx.ensure_selected_param_index_valid = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) EnsureSelectedParamIndexValid(*rt);
    };
    reload_ctx.ensure_selected_param_index_valid_user_data = &runtime;
    reload_ctx.sync_animation_channel_state = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) SyncAnimationChannelState(*rt);
    };
    reload_ctx.sync_animation_channel_state_user_data = &runtime;
    reload_ctx.set_editor_status = [](const std::string &text, float ttl_sec, void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) SetEditorStatus(*rt, text, ttl_sec);
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
        if (rt) SetClickThrough(*rt, enabled);
    };
    apply_ctx.set_click_through_user_data = &runtime;
    apply_ctx.sync_animation_channel_state = [](void *userdata) {
        auto *rt = static_cast<AppRuntime *>(userdata);
        if (rt) SyncAnimationChannelState(*rt);
    };
    apply_ctx.sync_animation_channel_state_user_data = &runtime;
    return apply_ctx;
}

void SetClickThrough(bool enabled) {
    SetClickThrough(g_runtime, enabled);
}

void SetClickThrough(AppRuntime &runtime, bool enabled) {
    runtime.click_through = enabled;

    if (runtime.entry_click_through) {
        SDL_SetTrayEntryChecked(runtime.entry_click_through, enabled);
    }

    k2d::ApplyWindowShape(runtime.window, runtime.window_w, runtime.window_h, runtime.interactive_rect, enabled);
}

void ToggleWindowVisibility() {
    k2d::ToggleWindowVisibility(g_runtime.window, &g_runtime.window_visible);
    k2d::UpdateWindowVisibilityLabel(g_runtime.entry_show_hide, g_runtime.window_visible);
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
    return k2d::ParamControllerContext{
        .model_loaded = &g_runtime.model_loaded,
        .manual_param_mode = &g_runtime.manual_param_mode,
        .selected_param_index = &g_runtime.selected_param_index,
        .model = &g_runtime.model,
    };
}

void SyncAnimationChannelState() {
    SyncAnimationChannelState(g_runtime);
}

void SyncAnimationChannelState(AppRuntime &runtime) {
    if (!runtime.model_loaded) {
        return;
    }
    runtime.model.animation_channels_enabled = !runtime.manual_param_mode;
}

bool HasModelParams() {
    return k2d::HasModelParams(BuildParamControllerContext());
}

void EnsureSelectedParamIndexValid() {
    EnsureSelectedParamIndexValid(g_runtime);
}

void EnsureSelectedParamIndexValid(AppRuntime &) {
    k2d::EnsureSelectedParamIndexValid(BuildParamControllerContext());
}

void CycleSelectedParam(int delta) {
    k2d::CycleSelectedParam(BuildParamControllerContext(), delta);
}

void AdjustSelectedParam(float delta) {
    k2d::AdjustSelectedParam(BuildParamControllerContext(), delta);
}

void ResetSelectedParam() {
    k2d::ResetSelectedParam(BuildParamControllerContext());
}

void ResetAllParams() {
    k2d::ResetAllParams(BuildParamControllerContext());
}

void ToggleManualParamMode() {
    k2d::ToggleManualParamMode(BuildParamControllerContext());
}

bool HasModelParts() {
    return g_runtime.model_loaded && !g_runtime.model.parts.empty();
}

void SetEditorStatus(std::string text, float ttl_sec) {
    SetEditorStatus(g_runtime, std::move(text), ttl_sec);
}

void SetEditorStatus(AppRuntime &runtime, std::string text, float ttl_sec) {
    runtime.editor_status = std::move(text);
    runtime.editor_status_ttl = std::max(0.0f, ttl_sec);
}

float QuantizeToGrid(float v, float grid) {
    const float g = std::max(0.001f, std::abs(grid));
    return std::round(v / g) * g;
}

const char *AxisConstraintName(AxisConstraint c) {
    switch (c) {
        case AxisConstraint::XOnly: return "X";
        case AxisConstraint::YOnly: return "Y";
        default: return "None";
    }
}


const char *EditorPropName(EditorProp p) {
    switch (p) {
        case EditorProp::PosX: return "posX";
        case EditorProp::PosY: return "posY";
        case EditorProp::PivotX: return "pivotX";
        case EditorProp::PivotY: return "pivotY";
        case EditorProp::RotDeg: return "rotDeg";
        case EditorProp::ScaleX: return "scaleX";
        case EditorProp::ScaleY: return "scaleY";
        default: return "unknown";
    }
}

void RenderModelHierarchyTree(ModelRuntime &model, int selected_part_index) {
    if (model.parts.empty()) {
        ImGui::TextDisabled("(no parts)");
        return;
    }

    if (ImGui::Button("Hide All")) {
        for (auto &p : model.parts) {
            p.base_opacity = 0.0f;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Show All")) {
        for (auto &p : model.parts) {
            p.base_opacity = 1.0f;
        }
    }

    std::vector<std::vector<int>> children(model.parts.size());
    std::vector<int> roots;
    roots.reserve(model.parts.size());

    for (int i = 0; i < static_cast<int>(model.parts.size()); ++i) {
        const int parent = model.parts[static_cast<std::size_t>(i)].parent_index;
        if (parent >= 0 && parent < static_cast<int>(model.parts.size())) {
            children[static_cast<std::size_t>(parent)].push_back(i);
        } else {
            roots.push_back(i);
        }
    }

    for (auto &v : children) {
        std::sort(v.begin(), v.end(), [&](int a, int b) {
            const auto &pa = model.parts[static_cast<std::size_t>(a)];
            const auto &pb = model.parts[static_cast<std::size_t>(b)];
            if (pa.draw_order != pb.draw_order) return pa.draw_order < pb.draw_order;
            return pa.id < pb.id;
        });
    }
    std::sort(roots.begin(), roots.end(), [&](int a, int b) {
        const auto &pa = model.parts[static_cast<std::size_t>(a)];
        const auto &pb = model.parts[static_cast<std::size_t>(b)];
        if (pa.draw_order != pb.draw_order) return pa.draw_order < pb.draw_order;
        return pa.id < pb.id;
    });

    auto draw_node = [&](auto &&self, int idx) -> void {
        auto &part = model.parts[static_cast<std::size_t>(idx)];
        const auto &sub = children[static_cast<std::size_t>(idx)];

        ImGui::PushID(idx);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (sub.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (idx == selected_part_index) flags |= ImGuiTreeNodeFlags_Selected;

        const std::string label = part.id + "##part_tree";
        const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

        ImGui::SameLine();
        const bool hidden = part.base_opacity <= 0.001f;
        if (ImGui::SmallButton(hidden ? "Show" : "Hide")) {
            part.base_opacity = hidden ? 1.0f : 0.0f;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("(draw=%d, parent=%d)", part.draw_order, part.parent_index);

        if (open) {
            for (int c : sub) {
                self(self, c);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    };

    for (int r : roots) {
        draw_node(draw_node, r);
    }
}

void UndoLastEdit() {
    const bool ok = k2d::UndoLastEdit(
        g_runtime.model,
        g_runtime.undo_stack,
        g_runtime.redo_stack,
        [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); });
    SetEditorStatus(ok ? "undo" : "undo empty", 1.0f);
}

void RedoLastEdit() {
    const bool ok = k2d::RedoLastEdit(
        g_runtime.model,
        g_runtime.undo_stack,
        g_runtime.redo_stack,
        [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); });
    SetEditorStatus(ok ? "redo" : "redo empty", 1.0f);
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

void EnsureSelectedPartIndexValid() {
    EnsureSelectedPartIndexValid(g_runtime);
}

void EnsureSelectedPartIndexValid(AppRuntime &runtime) {
    if (!(runtime.model_loaded && !runtime.model.parts.empty())) {
        runtime.selected_part_index = -1;
        return;
    }
    const int count = static_cast<int>(runtime.model.parts.size());
    if (runtime.selected_part_index < 0 || runtime.selected_part_index >= count) {
        runtime.selected_part_index = 0;
    }
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

int PickTopPartAt(float x, float y) {
    if (!HasModelParts()) {
        return -1;
    }

    for (auto it = g_runtime.model.draw_order_indices.rbegin();
         it != g_runtime.model.draw_order_indices.rend(); ++it) {
        const int idx = *it;
        if (idx < 0 || idx >= static_cast<int>(g_runtime.model.parts.size())) {
            continue;
        }

        const ModelPart &part = g_runtime.model.parts[static_cast<std::size_t>(idx)];
        if (part.runtime_opacity <= 0.01f) {
            continue;
        }

        if (PartContainsPointPrecise(part, x, y)) {
            return idx;
        }
    }

    return -1;
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

void SyncEditorControllerStateFromApp() {
    g_editor_state.dragging_part = g_runtime.dragging_part;
    g_editor_state.dragging_pivot = g_runtime.dragging_pivot;
    g_editor_state.drag_last_x = g_runtime.drag_last_x;
    g_editor_state.drag_last_y = g_runtime.drag_last_y;

    g_editor_state.gizmo_dragging = g_runtime.gizmo_dragging;
    g_editor_state.gizmo_hover_handle = g_runtime.gizmo_hover_handle;
    g_editor_state.gizmo_active_handle = g_runtime.gizmo_active_handle;
    g_editor_state.gizmo_drag_start_mouse_x = g_runtime.gizmo_drag_start_mouse_x;
    g_editor_state.gizmo_drag_start_mouse_y = g_runtime.gizmo_drag_start_mouse_y;
    g_editor_state.gizmo_drag_start_pos_x = g_runtime.gizmo_drag_start_pos_x;
    g_editor_state.gizmo_drag_start_pos_y = g_runtime.gizmo_drag_start_pos_y;
    g_editor_state.gizmo_drag_start_rot_deg = g_runtime.gizmo_drag_start_rot_deg;
    g_editor_state.gizmo_drag_start_scale_x = g_runtime.gizmo_drag_start_scale_x;
    g_editor_state.gizmo_drag_start_scale_y = g_runtime.gizmo_drag_start_scale_y;
    g_editor_state.gizmo_drag_start_angle = g_runtime.gizmo_drag_start_angle;
    g_editor_state.gizmo_drag_start_dist = g_runtime.gizmo_drag_start_dist;

    g_editor_state.edit_capture_active = g_runtime.edit_capture_active;
    g_editor_state.active_edit_cmd = g_runtime.active_edit_cmd;
}

void SyncAppStateFromEditorController() {
    g_runtime.dragging_part = g_editor_state.dragging_part;
    g_runtime.dragging_pivot = g_editor_state.dragging_pivot;
    g_runtime.drag_last_x = g_editor_state.drag_last_x;
    g_runtime.drag_last_y = g_editor_state.drag_last_y;

    g_runtime.gizmo_dragging = g_editor_state.gizmo_dragging;
    g_runtime.gizmo_hover_handle = g_editor_state.gizmo_hover_handle;
    g_runtime.gizmo_active_handle = g_editor_state.gizmo_active_handle;
    g_runtime.gizmo_drag_start_mouse_x = g_editor_state.gizmo_drag_start_mouse_x;
    g_runtime.gizmo_drag_start_mouse_y = g_editor_state.gizmo_drag_start_mouse_y;
    g_runtime.gizmo_drag_start_pos_x = g_editor_state.gizmo_drag_start_pos_x;
    g_runtime.gizmo_drag_start_pos_y = g_editor_state.gizmo_drag_start_pos_y;
    g_runtime.gizmo_drag_start_rot_deg = g_editor_state.gizmo_drag_start_rot_deg;
    g_runtime.gizmo_drag_start_scale_x = g_editor_state.gizmo_drag_start_scale_x;
    g_runtime.gizmo_drag_start_scale_y = g_editor_state.gizmo_drag_start_scale_y;
    g_runtime.gizmo_drag_start_angle = g_editor_state.gizmo_drag_start_angle;
    g_runtime.gizmo_drag_start_dist = g_editor_state.gizmo_drag_start_dist;

    g_runtime.edit_capture_active = g_editor_state.edit_capture_active;
    g_runtime.active_edit_cmd = g_editor_state.active_edit_cmd;
}

EditorControllerContext BuildEditorControllerContext() {
    return EditorControllerContext{
        .model = &g_runtime.model,
        .selected_part_index = &g_runtime.selected_part_index,
        .apply_pivot_delta = [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); },
        .has_model_parts = []() { return HasModelParts(); },
        .ensure_selected_part_index_valid = []() { EnsureSelectedPartIndexValid(); },
        .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
        .world_delta_to_parent_local = [](const ModelRuntime &model, int parent_index, float dx, float dy) {
            return WorldDeltaToParentLocal(model, parent_index, dx, dy);
        },
        .world_delta_to_part_local = [](const ModelPart &part, float dx, float dy) {
            return WorldDeltaToPartLocal(part, dx, dy);
        },
    };
}

void BeginDragPart(float mouse_x, float mouse_y) {
    SyncEditorControllerStateFromApp();
    const auto ctx = BuildEditorControllerContext();
    k2d::BeginDragPart(g_editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController();
}

void BeginDragPivot(float mouse_x, float mouse_y) {
    SyncEditorControllerStateFromApp();
    const auto ctx = BuildEditorControllerContext();
    k2d::BeginDragPivot(g_editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController();
}

void EndDragging() {
    SyncEditorControllerStateFromApp();
    const auto ctx = BuildEditorControllerContext();
    k2d::EndDragging(g_editor_state, ctx, g_runtime.undo_stack, g_runtime.redo_stack);
    SyncAppStateFromEditorController();
}

void BeginGizmoDrag(const ModelPart &part, GizmoHandle handle, float mouse_x, float mouse_y) {
    SyncEditorControllerStateFromApp();
    k2d::BeginGizmoDrag(g_editor_state, part, handle, mouse_x, mouse_y);
    SyncAppStateFromEditorController();
}

void EndGizmoDrag() {
    SyncEditorControllerStateFromApp();
    const auto ctx = BuildEditorControllerContext();
    k2d::EndGizmoDrag(g_editor_state, ctx, g_runtime.undo_stack, g_runtime.redo_stack);
    SyncAppStateFromEditorController();
}

void HandleGizmoDragMotion(float mouse_x, float mouse_y) {
    SyncEditorControllerStateFromApp();
    const auto ctx = BuildEditorControllerContext();
    k2d::HandleGizmoDragMotion(g_editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController();
}

void HandleEditorDragMotion(float mouse_x, float mouse_y) {
    SyncEditorControllerStateFromApp();
    const auto ctx = BuildEditorControllerContext();
    k2d::HandleEditorDragMotion(g_editor_state, ctx, mouse_x, mouse_y);
    SyncAppStateFromEditorController();
}

void SaveEditedModelJsonToDisk() {
    if (!g_runtime.model_loaded) {
        SetEditorStatus("save failed: model not loaded", 2.0f);
        return;
    }

    const std::string out_path = g_runtime.model.model_path.empty() ?
                                 "assets/model_01/model.json" : g_runtime.model.model_path;

    std::string err;
    const bool ok = SaveModelRuntimeJson(g_runtime.model, out_path.c_str(), &err);
    if (ok) {
        SetEditorStatus("saved model json: " + out_path, 2.5f);
    } else {
        SetEditorStatus("save failed: " + err, 3.5f);
    }
}



EditorInputBindingBridge BuildEditorInputBindingBridge(AppRuntime &runtime) {
    return EditorInputBindingBridge{
        .ensure_selected_part_index_valid = []() { EnsureSelectedPartIndexValid(); },
        .cycle_selected_part = [](bool shift) { CycleSelectedPart(shift ? -1 : 1); },
        .adjust_selected_param = [](float delta) { AdjustSelectedParam(delta); },
        .reset_selected_param = []() { ResetSelectedParam(); },
        .reset_all_params = []() { ResetAllParams(); },
        .toggle_edit_mode = [&runtime]() {
            runtime.edit_mode = !runtime.edit_mode;
            if (runtime.edit_mode) {
                EnsureSelectedPartIndexValid(runtime);
                SetEditorStatus(runtime, "edit mode ON", 1.5f);
            } else {
                EndDragging();
                EndGizmoDrag();
                runtime.gizmo_hover_handle = GizmoHandle::None;
                SetEditorStatus(runtime, "edit mode OFF", 1.5f);
            }
        },
        .toggle_manual_param_mode = []() { ToggleManualParamMode(); },
        .save_model = []() { SaveEditedModelJsonToDisk(); },
        .undo_edit = []() { UndoLastEdit(); },
        .redo_edit = []() { RedoLastEdit(); },
        .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
        .has_model_params = []() { return HasModelParams(); },
        .on_head_pat_mouse_motion = [&runtime](float mx, float my) {
            HandleHeadPatMouseMotion(
                runtime.interaction_state,
                InteractionControllerContext{
                    .model_loaded = runtime.model_loaded,
                    .model = &runtime.model,
                    .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                    .has_model_params = []() { return HasModelParams(); },
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
                    .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
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
            HandleAppRuntimeEvent(runtime, event, AppEventHandlerBridge{
                .build_editor_input_callbacks = [&runtime]() { return BuildEditorInputCallbacks(runtime); },
                .toggle_edit_mode = [&runtime]() {
                    runtime.edit_mode = !runtime.edit_mode;
                    if (runtime.edit_mode) {
                        EnsureSelectedPartIndexValid(runtime);
                        SetEditorStatus(runtime, "edit mode ON", 1.5f);
                    } else {
                        EndDragging();
                        EndGizmoDrag();
                        runtime.gizmo_hover_handle = GizmoHandle::None;
                        SetEditorStatus(runtime, "edit mode OFF", 1.5f);
                    }
                },
                .toggle_manual_param_mode = []() { ToggleManualParamMode(); },
                .cycle_selected_part = [](bool shift) { CycleSelectedPart(shift ? -1 : 1); },
                .adjust_selected_param = [](float delta) { AdjustSelectedParam(delta); },
                .reset_selected_param = []() { ResetSelectedParam(); },
                .reset_all_params = []() { ResetAllParams(); },
                .on_head_pat_mouse_motion = [&runtime](float mx, float my) {
                    HandleHeadPatMouseMotion(
                        runtime.interaction_state,
                        InteractionControllerContext{
                            .model_loaded = runtime.model_loaded,
                            .model = &runtime.model,
                            .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                            .has_model_params = []() { return HasModelParams(); },
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
                            .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                            .has_model_params = []() { return HasModelParams(); },
                        },
                        mx,
                        my);
                },
            });
        },
        .on_update = [&runtime](float dt) {
            RunRuntimeTickEntry(runtime, dt, RuntimeTickBridge{
                .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                .has_model_params = []() { return HasModelParams(); },
                .build_model_reload_context = [&runtime]() { return BuildModelReloadServiceContext(runtime); },
                .build_behavior_apply_context = [&runtime]() { return BuildBehaviorApplyContext(runtime); },
                .task_secondary_category_name = [](TaskSecondaryCategory c) { return TaskSecondaryCategoryName(c); },
                .infer_task_category_inplace = [&runtime]() {
                    InferTaskCategory(runtime.perception_state.system_context_snapshot,
                                      runtime.perception_state.ocr_result,
                                      runtime.perception_state.scene_result,
                                      runtime.task_primary,
                                      runtime.task_secondary);
                },
            });
        },
        .on_render = [&runtime]() {
            RunRuntimeRenderEntry(runtime, RuntimeRenderBridge{
                .has_model_parts = []() { return HasModelParts(); },
                .has_model_params = []() { return HasModelParams(); },
                .ensure_selected_part_index_valid = [&runtime]() { EnsureSelectedPartIndexValid(runtime); },
                .ensure_selected_param_index_valid = [&runtime]() { EnsureSelectedParamIndexValid(runtime); },
                .compute_part_aabb = [](const ModelPart &part, SDL_FRect *out_rect) {
                    return ComputePartAABB(part, out_rect);
                },
                .render_model_hierarchy_tree = [&runtime]() {
                    RenderModelHierarchyTree(runtime.model, runtime.selected_part_index);
                },
                .task_primary_category_name = [&runtime]() { return TaskPrimaryCategoryName(runtime.task_primary); },
                .task_secondary_category_name = [&runtime]() { return TaskSecondaryCategoryName(runtime.task_secondary); },
            });
        },
        .on_editor_status_expired = [&runtime]() {
            runtime.editor_status.clear();
        },
    });
}

}  // namespace k2d



