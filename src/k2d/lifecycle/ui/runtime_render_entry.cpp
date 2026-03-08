#include "k2d/lifecycle/ui/runtime_render_entry.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/ui/app_debug_ui.h"
#include "k2d/lifecycle/ui/reminder_panel.h"
#include "k2d/rendering/app_renderer.h"

namespace k2d {
namespace {

constexpr const char *kRuntimeDebugWindowName = "Runtime Debug";
constexpr const char *kInspectorWindowName = "Model Hierarchy + Inspector";
constexpr const char *kReminderWindowName = "Reminder";
constexpr const char *kWorkspaceDockspaceName = "Runtime Workspace DockSpace";

const char *WorkspaceModeName(WorkspaceMode mode) {
    switch (mode) {
        case WorkspaceMode::Animation: return "Animation";
        case WorkspaceMode::Debug: return "Debug";
        case WorkspaceMode::Perception: return "Perception";
        case WorkspaceMode::Authoring: return "Authoring";
        default: return "Unknown";
    }
}

#if defined(IMGUI_HAS_DOCK)
void ApplyWorkspaceDockLayout(AppRuntime &runtime, const ImGuiID dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_right = 0;
    ImGuiID dock_right_bottom = 0;

    const float right_ratio = runtime.workspace_mode == WorkspaceMode::Perception ? 0.22f : 0.34f;
    ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, right_ratio, &dock_right, &dock_main);

    if (runtime.workspace_mode != WorkspaceMode::Perception) {
        const float reminder_ratio = runtime.workspace_mode == WorkspaceMode::Animation ? 0.30f : 0.38f;
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, reminder_ratio, &dock_right_bottom, &dock_right);
    }

    ImGui::DockBuilderDockWindow(kRuntimeDebugWindowName, dock_main);

    if (runtime.workspace_mode == WorkspaceMode::Perception) {
        ImGui::DockBuilderDockWindow(kInspectorWindowName, dock_right);
        ImGui::DockBuilderDockWindow(kReminderWindowName, dock_right);
    } else {
        ImGui::DockBuilderDockWindow(kInspectorWindowName, dock_right);
        if (dock_right_bottom != 0) {
            ImGui::DockBuilderDockWindow(kReminderWindowName, dock_right_bottom);
        } else {
            ImGui::DockBuilderDockWindow(kReminderWindowName, dock_right);
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    runtime.workspace_docking_ini = ImGui::SaveIniSettingsToMemory();
    runtime.last_applied_workspace_mode = runtime.workspace_mode;
    runtime.workspace_dock_rebuild_requested = false;
    runtime.workspace_layout_reset_requested = false;
}
#endif

}  // namespace

void RunRuntimeRenderEntry(AppRuntime &runtime, const RuntimeRenderBridge &bridge) {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    RenderAppFrame(AppRenderContext{
        .renderer = runtime.renderer,
        .model_loaded = runtime.model_loaded,
        .model = &runtime.model,
        .demo_texture = runtime.demo_texture,
        .demo_texture_w = runtime.demo_texture_w,
        .demo_texture_h = runtime.demo_texture_h,
        .show_debug_stats = runtime.show_debug_stats,
        .manual_param_mode = runtime.manual_param_mode,
        .selected_param_index = runtime.selected_param_index,
        .edit_mode = runtime.edit_mode,
        .selected_part_index = runtime.selected_part_index,
        .gizmo_hover_handle = runtime.gizmo_hover_handle,
        .gizmo_active_handle = runtime.gizmo_active_handle,
        .editor_status = runtime.editor_status.c_str(),
        .editor_status_ttl = runtime.editor_status_ttl,
        .window_h = runtime.window_h,
        .debug_fps = runtime.debug_fps,
        .debug_frame_ms = runtime.debug_frame_ms,
        .has_model_parts = bridge.has_model_parts,
        .has_model_params = bridge.has_model_params,
        .ensure_selected_part_index_valid = bridge.ensure_selected_part_index_valid,
        .ensure_selected_param_index_valid = bridge.ensure_selected_param_index_valid,
        .compute_part_aabb = bridge.compute_part_aabb,
    });

    if (runtime.show_debug_stats) {
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGuiWindowFlags fps_flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav;
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        if (ImGui::Begin("FPS Overlay", nullptr, fps_flags)) {
            ImGui::Text("FPS: %.1f", runtime.debug_fps);
            ImGui::Text("Frame: %.2f ms", runtime.debug_frame_ms);
            ImGui::Text("Parts: %d/%d",
                        runtime.model.debug_stats.drawn_part_count,
                        runtime.model.debug_stats.part_count);
            ImGui::Text("Verts: %d  Tris: %d",
                        runtime.model.debug_stats.vertex_count,
                        runtime.model.debug_stats.triangle_count);
            ImGui::Separator();
            ImGui::Checkbox("Runtime Debug GUI", &runtime.gui_enabled);
        }
        ImGui::End();
    }

    if (runtime.gui_enabled) {
        const ImGuiViewport *vp = ImGui::GetMainViewport();
#if defined(IMGUI_HAS_DOCK)
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);

        ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking |
                                      ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoCollapse |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoNavFocus |
                                      ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin(kWorkspaceDockspaceName, nullptr, host_flags)) {
            ImGui::PopStyleVar(3);
            ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
            const ImGuiID dockspace_id = ImGui::GetID("RuntimeWorkspaceDockspaceId");

            if (runtime.workspace_docking_ini_pending_load &&
                !runtime.workspace_docking_ini.empty() &&
                !runtime.workspace_layout_reset_requested &&
                !runtime.workspace_layout_follow_preset) {
                ImGui::LoadIniSettingsFromMemory(runtime.workspace_docking_ini.c_str(), runtime.workspace_docking_ini.size());
                runtime.workspace_docking_ini_pending_load = false;
                runtime.last_applied_workspace_mode = runtime.workspace_mode;
                runtime.workspace_dock_rebuild_requested = false;
            }

            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

            if (runtime.workspace_layout_follow_preset &&
                (runtime.workspace_dock_rebuild_requested ||
                 runtime.workspace_layout_reset_requested ||
                 runtime.last_applied_workspace_mode != runtime.workspace_mode)) {
                ApplyWorkspaceDockLayout(runtime, dockspace_id);
                runtime.workspace_docking_ini_pending_load = false;
            }
        } else {
            ImGui::PopStyleVar(3);
        }
        ImGui::End();

        ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoCollapse;

        if (ImGui::Begin(kRuntimeDebugWindowName, nullptr, panel_flags)) {
            RenderWorkspaceToolbar(runtime);
            RenderAppDebugUi(runtime);
        }
        ImGui::End();

        if (!runtime.workspace_layout_reset_requested) {
            runtime.workspace_docking_ini = ImGui::SaveIniSettingsToMemory();
        }

        if (ImGui::Begin(kInspectorWindowName, nullptr, panel_flags)) {
            bridge.RenderResourceTreeInspector();
        }
        ImGui::End();

        bool reminder_open = runtime.workspace_mode != WorkspaceMode::Animation;
        if (!reminder_open && runtime.workspace_layout_follow_preset) {
            ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
        }
        if (ImGui::Begin(kReminderWindowName, nullptr, panel_flags)) {
            if (!reminder_open) {
                ImGui::TextDisabled("Workspace %s 默认弱化 Reminder 面板，可手动展开或切换布局跟随。",
                                    WorkspaceModeName(runtime.workspace_mode));
            }
            RenderReminderPanel(runtime);
        }
        ImGui::End();
#else
        const float base_x = vp ? vp->WorkPos.x : 0.0f;
        const float base_y = vp ? vp->WorkPos.y : 0.0f;
        const float work_w = vp ? vp->WorkSize.x : static_cast<float>(runtime.window_w);
        const float work_h = vp ? vp->WorkSize.y : static_cast<float>(runtime.window_h);

        const float margin = 12.0f;
        const float gap = 8.0f;
        const float top_offset = runtime.show_debug_stats ? 108.0f : 48.0f;

        const float usable_w = std::max(720.0f, work_w - margin * 2.0f);
        const float usable_h = std::max(420.0f, work_h - top_offset - margin);

        const bool compact_layout = usable_w < 1200.0f;
        const float debug_w = compact_layout ? usable_w : std::max(460.0f, usable_w * 0.52f);
        const float right_w = compact_layout ? usable_w : std::max(300.0f, usable_w - debug_w - gap);

        const float debug_h = compact_layout ? std::max(220.0f, usable_h * 0.52f) : usable_h;
        const float right_x = compact_layout ? (base_x + margin) : (base_x + margin + debug_w + gap);
        const float right_y = compact_layout ? (base_y + top_offset + debug_h + gap) : (base_y + top_offset);
        const float right_h = compact_layout ? std::max(220.0f, usable_h - debug_h - gap) : usable_h;
        const float inspector_h = compact_layout ? std::max(140.0f, right_h * 0.62f) : std::max(300.0f, right_h * 0.68f);
        const float reminder_h = std::max(120.0f, right_h - inspector_h - gap);

        ImGui::SetNextWindowPos(ImVec2(base_x + margin, base_y + top_offset), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(debug_w, debug_h), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(kRuntimeDebugWindowName)) {
            RenderWorkspaceToolbar(runtime);
            RenderAppDebugUi(runtime);
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(right_x, right_y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(right_w, inspector_h), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(kInspectorWindowName)) {
            bridge.RenderResourceTreeInspector();
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(right_x, right_y + inspector_h + gap), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(right_w, reminder_h), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(kReminderWindowName)) {
            RenderReminderPanel(runtime);
        }
        ImGui::End();
#endif
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), runtime.renderer);
    SDL_RenderPresent(runtime.renderer);
}

}  // namespace k2d
