#include "k2d/lifecycle/app_lifecycle.h"

#include <SDL3/SDL.h>
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
#include "k2d/lifecycle/ui/app_debug_ui.h"
#include "k2d/lifecycle/ui/reminder_panel.h"
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
void EnsureSelectedParamIndexValid();
void SyncAnimationChannelState();
void SetEditorStatus(std::string text, float ttl_sec = 2.0f);
void SetClickThrough(bool enabled);

ModelReloadServiceContext BuildModelReloadServiceContext() {
    ModelReloadServiceContext reload_ctx{};
    reload_ctx.dev_hot_reload_enabled = g_runtime.dev_hot_reload_enabled;
    reload_ctx.hot_reload_poll_accum_sec = &g_runtime.hot_reload_poll_accum_sec;
    reload_ctx.model_loaded = &g_runtime.model_loaded;
    reload_ctx.model = &g_runtime.model;
    reload_ctx.renderer = g_runtime.renderer;
    reload_ctx.model_time = &g_runtime.model_time;
    reload_ctx.selected_part_index = &g_runtime.selected_part_index;
    reload_ctx.model_last_write_time = &g_runtime.model_last_write_time;
    reload_ctx.model_last_write_time_valid = &g_runtime.model_last_write_time_valid;
    reload_ctx.ensure_selected_part_index_valid = [](void *) { EnsureSelectedPartIndexValid(); };
    reload_ctx.ensure_selected_param_index_valid = [](void *) { EnsureSelectedParamIndexValid(); };
    reload_ctx.sync_animation_channel_state = [](void *) { SyncAnimationChannelState(); };
    reload_ctx.set_editor_status = [](const std::string &text, float ttl_sec, void *) {
        SetEditorStatus(text, ttl_sec);
    };
    return reload_ctx;
}

BehaviorApplyContext BuildBehaviorApplyContext() {
    BehaviorApplyContext apply_ctx{};
    apply_ctx.model_loaded = g_runtime.model_loaded;
    apply_ctx.model = &g_runtime.model;
    apply_ctx.plugin_param_blend_mode = g_runtime.plugin_param_blend_mode;
    apply_ctx.window = g_runtime.window;
    apply_ctx.show_debug_stats = &g_runtime.show_debug_stats;
    apply_ctx.manual_param_mode = &g_runtime.manual_param_mode;
    apply_ctx.set_click_through = [](bool enabled, void *) { SetClickThrough(enabled); };
    apply_ctx.sync_animation_channel_state = [](void *) { SyncAnimationChannelState(); };
    return apply_ctx;
}

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

void SetEditorStatus(std::string text, float ttl_sec) {
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

const char *TaskPrimaryCategoryName(TaskPrimaryCategory c) {
    switch (c) {
        case TaskPrimaryCategory::Work: return "work";
        case TaskPrimaryCategory::Game: return "game";
        default: return "unknown";
    }
}

const char *TaskSecondaryCategoryName(TaskSecondaryCategory c) {
    switch (c) {
        case TaskSecondaryCategory::WorkCoding: return "coding";
        case TaskSecondaryCategory::WorkDebugging: return "debugging";
        case TaskSecondaryCategory::WorkReadingDocs: return "reading_docs";
        case TaskSecondaryCategory::WorkMeetingNotes: return "meeting_notes";
        case TaskSecondaryCategory::GameLobby: return "lobby";
        case TaskSecondaryCategory::GameMatch: return "match";
        case TaskSecondaryCategory::GameSettlement: return "settlement";
        case TaskSecondaryCategory::GameMenu: return "menu";
        default: return "unknown";
    }
}

std::string MakeUtf8SafeLabel(const std::string &s) {
    if (s.empty()) return s;

    std::string out;
    out.reserve(s.size());

    bool has_invalid = false;
    const auto *p = reinterpret_cast<const unsigned char *>(s.data());
    const std::size_t n = s.size();

    for (std::size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        if (c <= 0x7F) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }

        int need = 0;
        std::uint32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            cp = c & 0x07u;
        } else {
            has_invalid = true;
            ++i;
            continue;
        }

        if (i + static_cast<std::size_t>(need) >= n) {
            has_invalid = true;
            break;
        }

        bool ok = true;
        for (int k = 1; k <= need; ++k) {
            const unsigned char cc = p[i + static_cast<std::size_t>(k)];
            if ((cc & 0xC0u) != 0x80u) {
                ok = false;
                break;
            }
            cp = (cp << 6u) | (cc & 0x3Fu);
        }

        if (!ok) {
            has_invalid = true;
            ++i;
            continue;
        }

        if ((need == 1 && cp < 0x80u) ||
            (need == 2 && cp < 0x800u) ||
            (need == 3 && cp < 0x10000u) ||
            (cp >= 0xD800u && cp <= 0xDFFFu) ||
            (cp > 0x10FFFFu)) {
            has_invalid = true;
            ++i;
            continue;
        }

        out.append(s, i, static_cast<std::size_t>(need + 1));
        i += static_cast<std::size_t>(need + 1);
    }

    if (has_invalid) {
        return "[invalid-utf8]";
    }
    return out;
}

std::string MakeImguiAsciiSafe(const std::string &s) {
    if (s.empty()) return s;
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (ch >= 32 && ch <= 126) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    if (out.find_first_not_of('_') == std::string::npos) {
        return "[non-ascii-label]";
    }
    return out;
}

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary) {
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return s;
    };

    std::string text = ctx.process_name + "\n" + ctx.window_title + "\n" + ctx.url_hint + "\n" + ocr.summary + "\n" + scene.label;
    text = to_lower(text);

    out_primary = TaskPrimaryCategory::Unknown;
    out_secondary = TaskSecondaryCategory::Unknown;

    const bool has_game_hint =
        text.find("game") != std::string::npos ||
        text.find("steam") != std::string::npos ||
        text.find("unity") != std::string::npos ||
        text.find("ue4") != std::string::npos ||
        text.find("unreal") != std::string::npos ||
        text.find("lobby") != std::string::npos ||
        text.find("match") != std::string::npos ||
        text.find("battle") != std::string::npos ||
        text.find("menu") != std::string::npos ||
        text.find("settlement") != std::string::npos ||
        text.find("lobby") != std::string::npos ||
        text.find("match") != std::string::npos;

    if (has_game_hint) {
        out_primary = TaskPrimaryCategory::Game;
        if (text.find("result") != std::string::npos || text.find("settlement") != std::string::npos) {
            out_secondary = TaskSecondaryCategory::GameSettlement;
        } else if (text.find("lobby") != std::string::npos) {
            out_secondary = TaskSecondaryCategory::GameLobby;
        } else if (text.find("menu") != std::string::npos) {
            out_secondary = TaskSecondaryCategory::GameMenu;
        } else {
            out_secondary = TaskSecondaryCategory::GameMatch;
        }
        return;
    }

    out_primary = TaskPrimaryCategory::Work;
    if (text.find("debug") != std::string::npos || text.find("gdb") != std::string::npos) {
        out_secondary = TaskSecondaryCategory::WorkDebugging;
    } else if (text.find("readme") != std::string::npos || text.find("docs") != std::string::npos || text.find("wiki") != std::string::npos) {
        out_secondary = TaskSecondaryCategory::WorkReadingDocs;
    } else if (text.find("meeting") != std::string::npos || text.find("minutes") != std::string::npos) {
        out_secondary = TaskSecondaryCategory::WorkMeetingNotes;
    } else {
        out_secondary = TaskSecondaryCategory::WorkCoding;
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
        .gui_enabled = &g_runtime.gui_enabled,
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
            HandleHeadPatMouseMotion(
                g_runtime.interaction_state,
                InteractionControllerContext{
                    .model_loaded = g_runtime.model_loaded,
                    .model = &g_runtime.model,
                    .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                    .has_model_params = []() { return HasModelParams(); },
                },
                mx,
                my);

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
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                g_runtime.running = false;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                g_runtime.running = false;
            } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                g_runtime.window_w = event.window.data1;
                g_runtime.window_h = event.window.data2;
                g_runtime.interactive_rect = k2d::ComputeInteractiveRect(g_runtime.window_w, g_runtime.window_h);
                k2d::ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, g_runtime.click_through);
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                g_runtime.running = false;
            } else {
                if (!g_runtime.edit_mode) {
                    const bool imgui_wants_mouse = ImGui::GetIO().WantCaptureMouse;
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                        if (!imgui_wants_mouse) {
                            g_runtime.dragging_model_whole = true;
                            g_runtime.dragging_model_last_x = event.button.x;
                            g_runtime.dragging_model_last_y = event.button.y;
                        }
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
                        g_runtime.dragging_model_whole = false;
                    } else if (event.type == SDL_EVENT_MOUSE_MOTION && g_runtime.dragging_model_whole && g_runtime.model_loaded) {
                        if (imgui_wants_mouse) {
                            g_runtime.dragging_model_whole = false;
                        } else {
                            const float dx = event.motion.x - g_runtime.dragging_model_last_x;
                            const float dy = event.motion.y - g_runtime.dragging_model_last_y;
                            g_runtime.dragging_model_last_x = event.motion.x;
                            g_runtime.dragging_model_last_y = event.motion.y;

                            for (auto &part : g_runtime.model.parts) {
                                if (part.parent_index < 0) {
                                    part.base_pos_x += dx;
                                    part.base_pos_y += dy;
                                }
                            }
                        }
                    }
                }

                const auto non_edit_ctx = k2d::AppInputControllerContext{
                    .running = &g_runtime.running,
                    .show_debug_stats = &g_runtime.show_debug_stats,
                    .gui_enabled = &g_runtime.gui_enabled,
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
                auto reload_ctx = BuildModelReloadServiceContext();
                TryHotReloadModel(reload_ctx, dt);
                UpdateHeadPatReaction(
                    g_runtime.interaction_state,
                    InteractionControllerContext{
                        .model_loaded = g_runtime.model_loaded,
                        .model = &g_runtime.model,
                        .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                        .has_model_params = []() { return HasModelParams(); },
                    },
                    dt);
                UpdateModelRuntime(&g_runtime.model, g_runtime.model_time, dt);
            }

            if (g_runtime.model_loaded) {
                // 本地状态机输出
                BehaviorOutput local_out{};
                BuildInteractionBehaviorOutput(
                    g_runtime.interaction_state,
                    InteractionControllerContext{
                        .model_loaded = g_runtime.model_loaded,
                        .model = &g_runtime.model,
                        .pick_top_part_at = [](float x, float y) { return PickTopPartAt(x, y); },
                        .has_model_params = []() { return HasModelParams(); },
                    },
                    dt,
                    local_out);

                BehaviorOutput plugin_out{};
                bool has_plugin_out = false;
                if (g_runtime.plugin_ready) {
                    PerceptionInput in{};
                    in.time_sec = static_cast<double>(g_runtime.model_time);
                    in.audio_level = 0.0f;
                    in.user_presence = g_runtime.perception_state.face_emotion_result.face_detected ? 1.0f : (g_runtime.window_visible ? 1.0f : 0.0f);
                    in.vision.user_presence = in.user_presence;
                    in.vision.head_yaw_deg = 0.0f;
                    in.vision.head_pitch_deg = 0.0f;
                    in.vision.head_roll_deg = 0.0f;
                    in.vision.gaze_x = 0.0f;
                    in.vision.gaze_y = 0.0f;
                    in.scene_label = g_runtime.perception_state.scene_result.label;
                    in.task_label = TaskSecondaryCategoryName(g_runtime.task_secondary);
                    g_runtime.inference_adapter->SubmitInput(in);
                    has_plugin_out = g_runtime.inference_adapter->TryConsumeLatestOutput(plugin_out, nullptr);
                }

                // 统一 mixer：本地行为 + 插件行为同入口融合后再应用
                BehaviorMixResult mix_result{};
                std::vector<BehaviorMixSource> mix_sources;
                mix_sources.push_back(BehaviorMixSource{.name = "local_fsm", .output = &local_out, .global_weight = 1.0f});
                if (has_plugin_out) {
                    mix_sources.push_back(BehaviorMixSource{.name = "plugin", .output = &plugin_out, .global_weight = 1.0f});
                }

                if (MixBehaviorOutputs(mix_sources, &mix_result)) {
                    auto apply_ctx = BuildBehaviorApplyContext();
                    ApplyBehaviorOutput(mix_result.mixed, apply_ctx);
                }
            }

            TickAppSystems(g_runtime, dt);
            InferTaskCategory(g_runtime.perception_state.system_context_snapshot,
                              g_runtime.perception_state.ocr_result,
                              g_runtime.perception_state.scene_result,
                              g_runtime.task_primary,
                              g_runtime.task_secondary);
        },
        .on_render = []() {
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            RenderFrame();

            if (g_runtime.show_debug_stats) {
                ImGui::SetNextWindowBgAlpha(0.35f);
                ImGuiWindowFlags fps_flags = ImGuiWindowFlags_NoDecoration |
                                             ImGuiWindowFlags_AlwaysAutoResize |
                                             ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoFocusOnAppearing |
                                             ImGuiWindowFlags_NoNav;
                ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
                if (ImGui::Begin("FPS Overlay", nullptr, fps_flags)) {
                    ImGui::Text("FPS: %.1f", g_runtime.debug_fps);
                    ImGui::Text("Frame: %.2f ms", g_runtime.debug_frame_ms);
                    ImGui::Text("Parts: %d/%d",
                                g_runtime.model.debug_stats.drawn_part_count,
                                g_runtime.model.debug_stats.part_count);
                    ImGui::Text("Verts: %d  Tris: %d",
                                g_runtime.model.debug_stats.vertex_count,
                                g_runtime.model.debug_stats.triangle_count);
                }
                ImGui::End();
            }

            if (g_runtime.gui_enabled) {
                ImGui::SetNextWindowPos(ImVec2(12.0f, 120.0f), ImGuiCond_FirstUseEver);
                ImGui::Begin("Runtime Debug");
                RenderAppDebugUi(g_runtime);

                ImGui::SeparatorText("Model Hierarchy");
                RenderModelHierarchyTree(g_runtime.model, g_runtime.selected_part_index);

                ImGui::SeparatorText("Task Category");
                ImGui::Text("Primary: %s", TaskPrimaryCategoryName(g_runtime.task_primary));
                ImGui::Text("Secondary: %s", TaskSecondaryCategoryName(g_runtime.task_secondary));

                if (ImGui::Button("Close Program")) {
                    g_runtime.running = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Esc)");

                RenderReminderPanel(g_runtime);

                ImGui::End();
            }

            ImGui::Render();
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), g_runtime.renderer);
            SDL_RenderPresent(g_runtime.renderer);
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

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;

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

    if (!SDL_SetWindowOpacity(g_runtime.window, 1.0f)) {
        SDL_Log("SDL_SetWindowOpacity failed: %s", SDL_GetError());
    }

    g_runtime.click_through = runtime_cfg.click_through;
    g_runtime.window_visible = runtime_cfg.window_visible;
    g_runtime.show_debug_stats = runtime_cfg.show_debug_stats;
    g_runtime.manual_param_mode = runtime_cfg.manual_param_mode;
    g_runtime.dev_hot_reload_enabled = runtime_cfg.dev_hot_reload_enabled;
    g_runtime.plugin_param_blend_mode = runtime_cfg.plugin_param_blend_mode;

    SDL_GetWindowSize(g_runtime.window, &g_runtime.window_w, &g_runtime.window_h);
    g_runtime.interactive_rect = k2d::ComputeInteractiveRect(g_runtime.window_w, g_runtime.window_h);
    k2d::ApplyWindowShape(g_runtime.window, g_runtime.window_w, g_runtime.window_h, g_runtime.interactive_rect, g_runtime.click_through);

    if (!SDL_SetWindowHitTest(g_runtime.window, WindowHitTest, nullptr)) {
        SDL_Log("SDL_SetWindowHitTest failed: %s", SDL_GetError());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);

    if (!ImGui_ImplSDL3_InitForSDLRenderer(g_runtime.window, g_runtime.renderer)) {
        SDL_Log("ImGui_ImplSDL3_InitForSDLRenderer failed");
    }
    if (!ImGui_ImplSDLRenderer3_Init(g_runtime.renderer)) {
        SDL_Log("ImGui_ImplSDLRenderer3_Init failed");
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
        CommitStableModelBackup(g_runtime.model);
    } else {
        SDL_Log("%s", bootstrap.model_load_log.c_str());
    }

    g_runtime.inference_adapter = CreateDefaultInferenceAdapter();
    {
        PluginRuntimeConfig plugin_cfg{};
        plugin_cfg.show_debug_stats = g_runtime.show_debug_stats;
        plugin_cfg.manual_param_mode = g_runtime.manual_param_mode;
        plugin_cfg.click_through = g_runtime.click_through;
        plugin_cfg.window_opacity = 1.0f;

        PluginHostCallbacks host{};
        host.log = [](void *, const char *msg) {
            SDL_Log("[PluginHost] %s", msg ? msg : "");
        };
        host.user_data = nullptr;

        PluginWorkerConfig worker_cfg{};
        worker_cfg.update_hz = 60;
        worker_cfg.frame_budget_ms = 1;

        std::string plugin_err;
        g_runtime.plugin_ready = g_runtime.inference_adapter &&
                                 g_runtime.inference_adapter->Init(plugin_cfg, host, worker_cfg, &plugin_err);
        if (!g_runtime.plugin_ready) {
            SDL_Log("Inference adapter init failed: %s", plugin_err.c_str());
        }
    }

    {
        std::string reminder_err;
        const std::vector<std::string> reminder_db_candidates = {
            "assets/reminders.db",
            "../assets/reminders.db",
            "../../assets/reminders.db",
        };

        g_runtime.reminder_ready = false;
        for (const auto &db_path : reminder_db_candidates) {
            std::string try_err;
            if (g_runtime.reminder_service.Init(db_path, &try_err)) {
                g_runtime.reminder_ready = true;
                g_runtime.reminder_last_error.clear();
                SDL_Log("Reminder service init ok: %s", db_path.c_str());
                break;
            }
            reminder_err = try_err;
        }

        if (!g_runtime.reminder_ready) {
            g_runtime.reminder_last_error = reminder_err;
            SDL_Log("Reminder service init failed: %s", reminder_err.c_str());
        } else {
            g_runtime.reminder_upcoming = g_runtime.reminder_service.ListActive(32, nullptr);
        }
    }

    {
        std::string perception_err;
        g_runtime.perception_pipeline.Init(g_runtime.perception_state, &perception_err);
    }

    {
        std::unique_ptr<IAsrProvider> offline = std::make_unique<OfflineAsrProvider>("assets/whisper/ggml-base.bin");
        std::unique_ptr<IAsrProvider> cloud = std::make_unique<CloudAsrProvider>("https://api.openai.com/v1/audio/transcriptions", "YOUR_API_KEY");
        g_runtime.asr_provider = std::make_unique<HybridAsrProvider>(std::move(offline), std::move(cloud));

        std::string asr_err;
        g_runtime.asr_ready = g_runtime.asr_provider->Init(&asr_err);
        if (!g_runtime.asr_ready) {
            g_runtime.asr_last_error = asr_err;
            SDL_Log("ASR provider init failed: %s", asr_err.c_str());
        } else {
            g_runtime.asr_last_error.clear();
            SDL_Log("ASR provider init ok: %s", g_runtime.asr_provider->Name());
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
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    g_runtime.reminder_service.Shutdown();
    g_runtime.reminder_ready = false;

    if (g_runtime.inference_adapter) {
        g_runtime.inference_adapter->Shutdown();
        g_runtime.inference_adapter.reset();
    }
    g_runtime.plugin_ready = false;

    g_runtime.perception_pipeline.Shutdown(g_runtime.perception_state);

    if (g_runtime.asr_provider) {
        g_runtime.asr_provider->Shutdown();
        g_runtime.asr_provider.reset();
    }
    g_runtime.asr_ready = false;

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



