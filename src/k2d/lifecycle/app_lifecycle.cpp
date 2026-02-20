#include "k2d/lifecycle/app_lifecycle.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

#include "k2d/core/model.h"
#include "k2d/core/png_texture.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/editor/editor_gizmo.h"
#include "k2d/editor/editor_input.h"
#include "k2d/editor/editor_controller.h"
#include "k2d/rendering/app_renderer.h"
#include "k2d/controllers/app_bootstrap.h"
#include "k2d/controllers/window_controller.h"
#include "k2d/controllers/param_controller.h"
#include "k2d/controllers/app_loop.h"
#include "k2d/controllers/app_input_controller.h"
#include "k2d/lifecycle/plugin_lifecycle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace k2d {

namespace {

enum class AxisConstraint {
    None,
    XOnly,
    YOnly,
};

enum class EditorProp {
    PosX,
    PosY,
    PivotX,
    PivotY,
    RotDeg,
    ScaleX,
    ScaleY,
    Count,
};

struct AppRuntime {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Tray *tray = nullptr;
    SDL_TrayEntry *entry_click_through = nullptr;
    SDL_TrayEntry *entry_show_hide = nullptr;

    SDL_Texture *demo_texture = nullptr;
    int demo_texture_w = 0;
    int demo_texture_h = 0;

    ModelRuntime model;
    bool model_loaded = false;
    float model_time = 0.0f;

    bool running = true;

    // 开发态模型热重载
    bool dev_hot_reload_enabled = true;
    float hot_reload_poll_accum_sec = 0.0f;
    std::filesystem::file_time_type model_last_write_time{};
    bool model_last_write_time_valid = false;
    bool click_through = false;
    bool window_visible = true;
    int window_w = 0;
    int window_h = 0;
    SDL_Rect interactive_rect{0, 0, 0, 0};

    bool show_debug_stats = true;
    bool manual_param_mode = false;
    int selected_param_index = 0;

    bool edit_mode = false;
    int selected_part_index = -1;
    bool dragging_part = false;
    bool dragging_pivot = false;
    float drag_last_x = 0.0f;
    float drag_last_y = 0.0f;
    float drag_start_pos_x = 0.0f;
    float drag_start_pos_y = 0.0f;
    float drag_start_pivot_x = 0.0f;
    float drag_start_pivot_y = 0.0f;

    AxisConstraint axis_constraint = AxisConstraint::None;
    bool snap_enabled = false;
    float snap_grid = 10.0f;

    bool property_panel_enabled = true;
    int selected_editor_prop = 0;

    std::vector<EditCommand> undo_stack;
    std::vector<EditCommand> redo_stack;

    GizmoHandle gizmo_hover_handle = GizmoHandle::None;
    GizmoHandle gizmo_active_handle = GizmoHandle::None;
    bool gizmo_dragging = false;
    float gizmo_drag_start_mouse_x = 0.0f;
    float gizmo_drag_start_mouse_y = 0.0f;
    float gizmo_drag_start_pos_x = 0.0f;
    float gizmo_drag_start_pos_y = 0.0f;
    float gizmo_drag_start_rot_deg = 0.0f;
    float gizmo_drag_start_scale_x = 1.0f;
    float gizmo_drag_start_scale_y = 1.0f;
    float gizmo_drag_start_angle = 0.0f;
    float gizmo_drag_start_dist = 1.0f;

    bool edit_capture_active = false;
    EditCommand active_edit_cmd;

    std::string editor_status;
    float editor_status_ttl = 0.0f;

    float debug_fps = 0.0f;
    float debug_frame_ms = 0.0f;
    float debug_fps_accum_sec = 0.0f;
    int debug_fps_accum_frames = 0;

    PluginManager plugin_manager;
    bool plugin_ready = false;
};

AppRuntime g_runtime;
EditorControllerState g_editor_state;

void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y);
void EnsureSelectedPartIndexValid();


void SetClickThrough(bool enabled) {
    g_runtime.click_through = enabled;

    if (g_runtime.entry_click_through) {
        SDL_SetTrayEntryChecked(g_runtime.entry_click_through, enabled);
    }

    k2d::ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, enabled);
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
    return k2d::WindowHitTest(g_runtime.click_through, g_runtime.interactive_rect, area);
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
    if (!g_runtime.model_loaded) {
        return;
    }
    g_runtime.model.animation_channels_enabled = !g_runtime.manual_param_mode;
}

bool HasModelParams() {
    return k2d::HasModelParams(BuildParamControllerContext());
}

void EnsureSelectedParamIndexValid() {
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

void SetEditorStatus(std::string text, float ttl_sec = 2.0f) {
    g_runtime.editor_status = std::move(text);
    g_runtime.editor_status_ttl = std::max(0.0f, ttl_sec);
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
    if (!HasModelParts()) {
        g_runtime.selected_part_index = -1;
        return;
    }
    const int count = static_cast<int>(g_runtime.model.parts.size());
    if (g_runtime.selected_part_index < 0 || g_runtime.selected_part_index >= count) {
        g_runtime.selected_part_index = 0;
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

void TryHotReloadModel(float dt_sec) {
    if (!g_runtime.dev_hot_reload_enabled || !g_runtime.model_loaded) {
        return;
    }
    if (g_runtime.model.model_path.empty()) {
        return;
    }

    g_runtime.hot_reload_poll_accum_sec += std::max(0.0f, dt_sec);
    if (g_runtime.hot_reload_poll_accum_sec < 0.5f) {
        return;
    }
    g_runtime.hot_reload_poll_accum_sec = 0.0f;

    std::error_code ec;
    const auto now_write_time = std::filesystem::last_write_time(g_runtime.model.model_path, ec);
    if (ec) {
        return;
    }

    if (!g_runtime.model_last_write_time_valid) {
        g_runtime.model_last_write_time = now_write_time;
        g_runtime.model_last_write_time_valid = true;
        return;
    }

    if (now_write_time == g_runtime.model_last_write_time) {
        return;
    }

    ModelRuntime new_model;
    std::string load_err;
    if (!LoadModelRuntime(g_runtime.renderer, g_runtime.model.model_path.c_str(), &new_model, &load_err)) {
        SetEditorStatus("hot reload failed: " + load_err, 3.0f);
        return;
    }

    DestroyModelRuntime(&g_runtime.model);
    g_runtime.model = std::move(new_model);
    g_runtime.model_loaded = true;
    g_runtime.model_time = 0.0f;
    g_runtime.model_last_write_time = now_write_time;
    g_runtime.model_last_write_time_valid = true;

    g_runtime.selected_part_index = -1;
    EnsureSelectedPartIndexValid();
    EnsureSelectedParamIndexValid();
    SyncAnimationChannelState();

    SetEditorStatus("model hot reloaded", 1.5f);
}

void RenderFrame() {
    k2d::RenderAppFrame(k2d::AppRenderContext{
        .renderer = g_runtime.renderer,
        .model_loaded = g_runtime.model_loaded,
        .model = &g_runtime.model,
        .demo_texture = g_runtime.demo_texture,
        .demo_texture_w = g_runtime.demo_texture_w,
        .demo_texture_h = g_runtime.demo_texture_h,
        .show_debug_stats = g_runtime.show_debug_stats,
        .manual_param_mode = g_runtime.manual_param_mode,
        .selected_param_index = g_runtime.selected_param_index,
        .edit_mode = g_runtime.edit_mode,
        .selected_part_index = g_runtime.selected_part_index,
        .gizmo_hover_handle = g_runtime.gizmo_hover_handle,
        .gizmo_active_handle = g_runtime.gizmo_active_handle,
        .editor_status = g_runtime.editor_status.c_str(),
        .editor_status_ttl = g_runtime.editor_status_ttl,
        .window_h = g_runtime.window_h,
        .debug_fps = g_runtime.debug_fps,
        .debug_frame_ms = g_runtime.debug_frame_ms,
        .has_model_parts = []() { return HasModelParts(); },
        .has_model_params = []() { return HasModelParams(); },
        .ensure_selected_part_index_valid = []() { EnsureSelectedPartIndexValid(); },
        .ensure_selected_param_index_valid = []() { EnsureSelectedParamIndexValid(); },
        .compute_part_aabb = [](const ModelPart &part, SDL_FRect *out_rect) {
            return ComputePartAABB(part, out_rect);
        },
    });
}
}  // namespace

k2d::EditorInputCallbacks BuildEditorInputCallbacks() {
    const auto ctx = k2d::AppInputControllerContext{
        .running = &g_runtime.running,
        .show_debug_stats = &g_runtime.show_debug_stats,
        .edit_mode = &g_runtime.edit_mode,
        .manual_param_mode = &g_runtime.manual_param_mode,
        .toggle_edit_mode = []() {
            g_runtime.edit_mode = !g_runtime.edit_mode;
            if (g_runtime.edit_mode) {
                EnsureSelectedPartIndexValid();
                SetEditorStatus("edit mode ON", 1.5f);
            } else {
                EndDragging();
                EndGizmoDrag();
                g_runtime.gizmo_hover_handle = GizmoHandle::None;
                SetEditorStatus("edit mode OFF", 1.5f);
            }
        },
        .toggle_manual_param_mode = []() { ToggleManualParamMode(); },
        .cycle_selected_part = [](bool shift) { CycleSelectedPart(shift ? -1 : 1); },
        .adjust_selected_param = [](float delta) { AdjustSelectedParam(delta); },
        .reset_selected_param = []() { ResetSelectedParam(); },
        .reset_all_params = []() { ResetAllParams(); },
        .save_model = []() { SaveEditedModelJsonToDisk(); },
        .undo_edit = []() { UndoLastEdit(); },
        .redo_edit = []() { RedoLastEdit(); },
        .on_mouse_button_down = [](float mx, float my, bool shift_pressed, Uint8 button) {
            if (button == SDL_BUTTON_LEFT) {
                EnsureSelectedPartIndexValid();
                if (!shift_pressed && g_runtime.selected_part_index >= 0 &&
                    g_runtime.selected_part_index < static_cast<int>(g_runtime.model.parts.size())) {
                    const ModelPart &selected = g_runtime.model.parts[static_cast<std::size_t>(g_runtime.selected_part_index)];
                    const GizmoHandle handle = k2d::PickGizmoHandle(selected, mx, my);
                    g_runtime.gizmo_hover_handle = handle;
                    if (handle != GizmoHandle::None) {
                        EndDragging();
                        BeginGizmoDrag(selected, handle, mx, my);
                        return;
                    }
                }
                EndGizmoDrag();
                if (shift_pressed) {
                    BeginDragPivot(mx, my);
                } else {
                    BeginDragPart(mx, my);
                }
            } else if (button == SDL_BUTTON_RIGHT) {
                EndGizmoDrag();
                BeginDragPivot(mx, my);
            }
        },
        .on_mouse_button_up = []() {
            EndDragging();
            EndGizmoDrag();
        },
        .on_mouse_motion = [](float mx, float my) {
            if (g_runtime.gizmo_dragging) {
                HandleGizmoDragMotion(mx, my);
            } else {
                HandleEditorDragMotion(mx, my);
                EnsureSelectedPartIndexValid();
                if (g_runtime.selected_part_index >= 0 &&
                    g_runtime.selected_part_index < static_cast<int>(g_runtime.model.parts.size())) {
                    const ModelPart &selected = g_runtime.model.parts[static_cast<std::size_t>(g_runtime.selected_part_index)];
                    g_runtime.gizmo_hover_handle = k2d::PickGizmoHandle(selected, mx, my);
                } else {
                    g_runtime.gizmo_hover_handle = GizmoHandle::None;
                }
            }
        },
    };

    return k2d::BuildEditorInputCallbacks(ctx);
}

void AppLifecycleRun(AppLifecycleContext &ctx) {
    k2d::RunAppLoop(k2d::AppLoopContext{
        .running = &g_runtime.running,
        .window_visible = &g_runtime.window_visible,
        .model_time = &g_runtime.model_time,
        .editor_status_ttl = &g_runtime.editor_status_ttl,
        .debug_frame_ms = &g_runtime.debug_frame_ms,
        .debug_fps = &g_runtime.debug_fps,
        .debug_fps_accum_sec = &g_runtime.debug_fps_accum_sec,
        .debug_fps_accum_frames = &g_runtime.debug_fps_accum_frames,
        .handle_event = [](const SDL_Event &event) {
            if (event.type == SDL_EVENT_QUIT) {
                g_runtime.running = false;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                g_runtime.running = false;
            } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                g_runtime.window_w = event.window.data1;
                g_runtime.window_h = event.window.data2;
                g_runtime.interactive_rect = k2d::ComputeInteractiveRect(g_runtime.window_w, g_runtime.window_h);
                k2d::ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, g_runtime.click_through);
            } else {
                const auto non_edit_ctx = k2d::AppInputControllerContext{
                    .running = &g_runtime.running,
                    .show_debug_stats = &g_runtime.show_debug_stats,
                    .edit_mode = &g_runtime.edit_mode,
                    .manual_param_mode = &g_runtime.manual_param_mode,
                    .toggle_edit_mode = []() {
                        g_runtime.edit_mode = !g_runtime.edit_mode;
                        if (g_runtime.edit_mode) {
                            EnsureSelectedPartIndexValid();
                            SetEditorStatus("edit mode ON", 1.5f);
                        } else {
                            EndDragging();
                            EndGizmoDrag();
                            g_runtime.gizmo_hover_handle = GizmoHandle::None;
                            SetEditorStatus("edit mode OFF", 1.5f);
                        }
                    },
                    .toggle_manual_param_mode = []() { ToggleManualParamMode(); },
                    .cycle_selected_part = [](bool shift) { CycleSelectedPart(shift ? -1 : 1); },
                    .adjust_selected_param = [](float delta) { AdjustSelectedParam(delta); },
                    .reset_selected_param = []() { ResetSelectedParam(); },
                    .reset_all_params = []() { ResetAllParams(); },
                };
                k2d::HandleAppInputEvent(event, g_runtime.edit_mode, BuildEditorInputCallbacks(), non_edit_ctx);
            }
        },
        .on_update = [](float dt) {
            if (g_runtime.model_loaded) {
                TryHotReloadModel(dt);
                UpdateModelRuntime(&g_runtime.model, g_runtime.model_time, dt);
            }

            if (g_runtime.plugin_ready) {
                PerceptionInput in{};
                in.time_sec = static_cast<double>(g_runtime.model_time);
                in.delta_time_sec = dt;
                in.has_user_focus = true;
                in.has_pointer_inside_window = false;
                in.audio_level = 0.0f;

                BehaviorOutput out{};
                std::string plugin_err;
                const PluginStatus st = g_runtime.plugin_manager.Update(in, out, &plugin_err);
                if (st == PluginStatus::Ok) {
                    if (out.has_request_show_debug_stats) {
                        g_runtime.show_debug_stats = out.request_show_debug_stats;
                    }
                    if (out.has_request_manual_param_mode) {
                        g_runtime.manual_param_mode = out.request_manual_param_mode;
                        SyncAnimationChannelState();
                    }
                    if (out.has_request_click_through) {
                        SetClickThrough(out.request_click_through);
                    }
                    if (out.has_target_opacity && g_runtime.window) {
                        const float opacity = std::clamp(out.target_opacity, 0.05f, 1.0f);
                        if (!SDL_SetWindowOpacity(g_runtime.window, opacity)) {
                            SDL_Log("Plugin SetWindowOpacity failed: %s", SDL_GetError());
                        }
                    }
                } else {
                    SDL_Log("Plugin update failed: %s", plugin_err.c_str());
                }
            }
        },
        .on_render = []() {
            RenderFrame();
        },
        .on_editor_status_expired = []() {
            g_runtime.editor_status.clear();
        },
    });
    (void)ctx;
}

bool AppLifecycleInit(AppLifecycleContext &ctx) {
    (void)ctx.argc;
    (void)ctx.argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        ctx.exit_code = 1;
        return false;
    }

    const SDL_WindowFlags flags = SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS |
                                  SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY;

    const AppRuntimeConfig runtime_cfg = LoadRuntimeConfig();

    g_runtime.window = SDL_CreateWindow("Overlay",
                                        runtime_cfg.window_width,
                                        runtime_cfg.window_height,
                                        flags);
    if (!g_runtime.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        ctx.exit_code = 1;
        return false;
    }

    g_runtime.renderer = SDL_CreateRenderer(g_runtime.window, nullptr);
    if (!g_runtime.renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(g_runtime.window);
        g_runtime.window = nullptr;
        SDL_Quit();
        ctx.exit_code = 1;
        return false;
    }

    if (!SDL_SetWindowOpacity(g_runtime.window, runtime_cfg.window_opacity)) {
        SDL_Log("SDL_SetWindowOpacity failed: %s", SDL_GetError());
    }

    g_runtime.click_through = runtime_cfg.click_through;
    g_runtime.window_visible = runtime_cfg.window_visible;
    g_runtime.show_debug_stats = runtime_cfg.show_debug_stats;
    g_runtime.manual_param_mode = runtime_cfg.manual_param_mode;
    g_runtime.dev_hot_reload_enabled = runtime_cfg.dev_hot_reload_enabled;

    SDL_GetWindowSize(g_runtime.window, &g_runtime.window_w, &g_runtime.window_h);
    g_runtime.interactive_rect = k2d::ComputeInteractiveRect(g_runtime.window_w, g_runtime.window_h);
    k2d::ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, g_runtime.click_through);

    if (!SDL_SetWindowHitTest(g_runtime.window, WindowHitTest, nullptr)) {
        SDL_Log("SDL_SetWindowHitTest failed: %s", SDL_GetError());
    }

    ctx.exit_code = 0;
    return true;
}

bool AppLifecycleBootstrap(AppLifecycleContext &ctx) {
    AppBootstrapResult bootstrap = BootstrapModelAndResources(g_runtime.renderer);

    g_runtime.model_loaded = bootstrap.model_loaded;
    if (bootstrap.runtime_config.default_model_candidates.empty()) {
        bootstrap.runtime_config.default_model_candidates = {
            "assets/model_01/model.json",
            "../assets/model_01/model.json",
            "../../assets/model_01/model.json",
        };
    }
    if (g_runtime.model_loaded) {
        g_runtime.model = bootstrap.model;
        SDL_Log("%s", bootstrap.model_load_log.c_str());
        g_runtime.selected_param_index = 0;
        SyncAnimationChannelState();

        std::error_code ec;
        const auto write_time = std::filesystem::last_write_time(g_runtime.model.model_path, ec);
        g_runtime.model_last_write_time_valid = !ec;
        if (!ec) {
            g_runtime.model_last_write_time = write_time;
        }
    } else {
        SDL_Log("%s", bootstrap.model_load_log.c_str());
    }

    g_runtime.plugin_manager.SetPlugin(CreateDefaultBehaviorPlugin());
    {
        PluginRuntimeConfig plugin_cfg{};
        plugin_cfg.show_debug_stats = g_runtime.show_debug_stats;
        plugin_cfg.manual_param_mode = g_runtime.manual_param_mode;
        plugin_cfg.click_through = g_runtime.click_through;
        plugin_cfg.window_opacity = bootstrap.runtime_config.window_opacity;

        PluginHostCallbacks host{};
        host.log = [](void *, const char *msg) {
            SDL_Log("[PluginHost] %s", msg ? msg : "");
        };
        host.user_data = nullptr;

        std::string plugin_err;
        const PluginStatus st = g_runtime.plugin_manager.Init(plugin_cfg, host, &plugin_err);
        g_runtime.plugin_ready = (st == PluginStatus::Ok);
        if (!g_runtime.plugin_ready) {
            SDL_Log("Plugin init failed: %s", plugin_err.c_str());
        } else {
            const auto &desc = g_runtime.plugin_manager.Descriptor();
            SDL_Log("Plugin initialized: name=%s version=%s capabilities=%s",
                    desc.name ? desc.name : "unknown",
                    desc.version ? desc.version : "unknown",
                    desc.capabilities ? desc.capabilities : "");
        }
    }

    g_runtime.demo_texture = bootstrap.demo_texture;
    g_runtime.demo_texture_w = bootstrap.demo_texture_w;
    g_runtime.demo_texture_h = bootstrap.demo_texture_h;
    if (!g_runtime.demo_texture) {
        SDL_Log("Failed to load test.png: %s", bootstrap.demo_texture_error.c_str());
    }

    SDL_Surface *tray_icon = k2d::CreateTrayIconSurface();
    g_runtime.tray = SDL_CreateTray(tray_icon, "SDL Overlay");
    if (tray_icon) {
        SDL_DestroySurface(tray_icon);
    }

    if (g_runtime.tray) {
        SDL_TrayMenu *menu = SDL_CreateTrayMenu(g_runtime.tray);
        if (menu) {
            g_runtime.entry_click_through = SDL_InsertTrayEntryAt(menu, -1, "Click-Through", SDL_TRAYENTRY_CHECKBOX);
            if (g_runtime.entry_click_through) {
                SDL_SetTrayEntryChecked(g_runtime.entry_click_through, g_runtime.click_through);
                SDL_SetTrayEntryCallback(g_runtime.entry_click_through, TrayToggleClickThrough, nullptr);
            }

            g_runtime.entry_show_hide = SDL_InsertTrayEntryAt(menu,
                                                              -1,
                                                              g_runtime.window_visible ? "Hide Window" : "Show Window",
                                                              SDL_TRAYENTRY_BUTTON);
            if (g_runtime.entry_show_hide) {
                SDL_SetTrayEntryCallback(g_runtime.entry_show_hide, TrayToggleVisibility, nullptr);
            }

            SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);

            SDL_TrayEntry *entry_quit = SDL_InsertTrayEntryAt(menu, -1, "Quit", SDL_TRAYENTRY_BUTTON);
            if (entry_quit) {
                SDL_SetTrayEntryCallback(entry_quit, TrayQuit, nullptr);
            }
        }
    } else {
        SDL_Log("SDL_CreateTray failed: %s", SDL_GetError());
    }

    if (!g_runtime.window_visible && g_runtime.window) {
        SDL_HideWindow(g_runtime.window);
    }

    ctx.exit_code = 0;
    return true;
}

void AppLifecycleTeardown(AppLifecycleContext &ctx) {
    g_runtime.plugin_manager.Destroy();
    g_runtime.plugin_ready = false;

    if (g_runtime.tray) {
        SDL_DestroyTray(g_runtime.tray);
        g_runtime.tray = nullptr;
    }

    DestroyModelRuntime(&g_runtime.model);

    if (g_runtime.demo_texture) {
        SDL_DestroyTexture(g_runtime.demo_texture);
        g_runtime.demo_texture = nullptr;
    }

    if (g_runtime.renderer) {
        SDL_DestroyRenderer(g_runtime.renderer);
        g_runtime.renderer = nullptr;
    }
    if (g_runtime.window) {
        SDL_DestroyWindow(g_runtime.window);
        g_runtime.window = nullptr;
    }
    SDL_Quit();
    ctx.exit_code = 0;
}


}  // namespace k2d



