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

constexpr const char *kOverviewWindowName = "Runtime Overview";
constexpr const char *kEditorWindowName = "Runtime Editor";
constexpr const char *kTimelineWindowName = "Runtime Timeline";
constexpr const char *kPerceptionWindowName = "Runtime Perception";
constexpr const char *kMappingWindowName = "Runtime Mapping";
constexpr const char *kAsrChatWindowName = "Runtime ASR + Chat";
constexpr const char *kErrorWindowName = "Runtime Errors";
constexpr const char *kOpsWindowName = "Runtime Ops";
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
void ApplyWorkspacePresetVisibility(AppRuntime &runtime) {
    runtime.show_workspace_window = true;
    runtime.show_overview_window = true;
    runtime.show_editor_window = true;
    runtime.show_timeline_window = true;
    runtime.show_perception_window = true;
    runtime.show_mapping_window = true;
    runtime.show_asr_chat_window = true;
    runtime.show_error_window = true;
    runtime.show_ops_window = true;
    runtime.show_inspector_window = true;
    runtime.show_reminder_window = true;

    switch (runtime.workspace_mode) {
        case WorkspaceMode::Debug:
            runtime.show_editor_window = false;
            runtime.show_timeline_window = false;
            runtime.show_mapping_window = false;
            break;
        case WorkspaceMode::Perception:
            runtime.show_editor_window = false;
            runtime.show_timeline_window = false;
            runtime.show_mapping_window = false;
            runtime.show_asr_chat_window = false;
            break;
        case WorkspaceMode::Animation:
            runtime.show_error_window = false;
            runtime.show_ops_window = false;
            runtime.show_asr_chat_window = false;
            runtime.show_reminder_window = false;
            break;
        case WorkspaceMode::Authoring:
            runtime.show_asr_chat_window = false;
            break;
        default:
            break;
    }
}

void ApplyWorkspaceDockLayout(AppRuntime &runtime, const ImGuiID dockspace_id) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
    const ImVec2 safe_size(std::max(work_size.x, 1.0f), std::max(work_size.y, 1.0f));
    ImGui::DockBuilderSetNodeSize(dockspace_id, safe_size);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_left = 0;
    ImGuiID dock_right = 0;
    ImGuiID dock_bottom = 0;
    ImGuiID dock_right_bottom = 0;

    switch (runtime.workspace_mode) {
        case WorkspaceMode::Debug: {
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.24f, &dock_left, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.30f, &dock_right, &dock_main);
            ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.32f, &dock_right_bottom, &dock_right);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.24f, &dock_bottom, &dock_main);

            ImGui::DockBuilderDockWindow(kOverviewWindowName, dock_left);
            ImGui::DockBuilderDockWindow(kErrorWindowName, dock_main);
            ImGui::DockBuilderDockWindow(kPerceptionWindowName, dock_right);
            ImGui::DockBuilderDockWindow(kOpsWindowName, dock_right_bottom);
            ImGui::DockBuilderDockWindow(kAsrChatWindowName, dock_bottom);
            ImGui::DockBuilderDockWindow(kInspectorWindowName, dock_left);
            ImGui::DockBuilderDockWindow(kReminderWindowName, dock_right_bottom);
            break;
        }
        case WorkspaceMode::Perception: {
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.34f, &dock_left, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.30f, &dock_right, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.22f, &dock_bottom, &dock_main);

            ImGui::DockBuilderDockWindow(kPerceptionWindowName, dock_left);
            ImGui::DockBuilderDockWindow(kOverviewWindowName, dock_main);
            ImGui::DockBuilderDockWindow(kErrorWindowName, dock_right);
            ImGui::DockBuilderDockWindow(kOpsWindowName, dock_bottom);
            ImGui::DockBuilderDockWindow(kReminderWindowName, dock_bottom);
            ImGui::DockBuilderDockWindow(kInspectorWindowName, dock_right);
            break;
        }
        case WorkspaceMode::Animation:
        case WorkspaceMode::Authoring: {
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.28f, &dock_left, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.24f, &dock_right, &dock_main);
            ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.22f, &dock_bottom, &dock_main);

            ImGui::DockBuilderDockWindow(kEditorWindowName, dock_left);
            ImGui::DockBuilderDockWindow(kTimelineWindowName, dock_main);
            ImGui::DockBuilderDockWindow(kMappingWindowName, dock_right);
            ImGui::DockBuilderDockWindow(kOverviewWindowName, dock_bottom);
            ImGui::DockBuilderDockWindow(kInspectorWindowName, dock_right);
            ImGui::DockBuilderDockWindow(kReminderWindowName, dock_bottom);
            ImGui::DockBuilderDockWindow(kPerceptionWindowName, dock_left);
            break;
        }
        default:
            ImGui::DockBuilderDockWindow(kOverviewWindowName, dock_main);
            break;
    }

    ImGui::DockBuilderFinish(dockspace_id);
    // Preset 仅负责 Dock 分配，不在每次重建时覆盖窗口可见性开关。
    runtime.last_applied_workspace_mode = runtime.workspace_mode;
    runtime.workspace_preset_apply_requested = false;
    runtime.workspace_dock_rebuild_requested = false;
    runtime.workspace_manual_layout_reset_requested = false;
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

    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false) && !io.WantCaptureKeyboard) {
        runtime.gui_enabled = !runtime.gui_enabled;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantCaptureKeyboard) {
        runtime.running = false;
    }

    if (runtime.show_debug_stats) {
        ImGui::SetNextWindowBgAlpha(0.25f);
        ImGuiWindowFlags fps_flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav |
                                     ImGuiWindowFlags_NoInputs;
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
            ImGui::TextUnformatted("F1: Toggle GUI");
            ImGui::TextUnformatted("Esc: Close Program");
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

            const bool docking_window_drag_active =
                ImGui::IsDragDropActive() &&
                GImGui != nullptr &&
                GImGui->DragDropPayload.IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW);
            const bool docking_dragging_active =
                ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                docking_window_drag_active;

            enum class DockingTransition {
                None,
                ManualRestore,
                PresetRebuild,
            };

            const bool manual_restore_requested =
                runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual &&
                runtime.workspace_manual_layout_pending_load &&
                !runtime.workspace_manual_docking_ini.empty();

            const bool preset_rebuild_requested =
                runtime.workspace_dock_rebuild_requested ||
                (runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset &&
                 (runtime.workspace_preset_apply_requested ||
                  runtime.last_applied_workspace_mode != runtime.workspace_mode));

            // 单向状态机：同帧最多发生一种转移，优先 ManualRestore，再 PresetRebuild。
            DockingTransition transition = DockingTransition::None;
            if (!docking_dragging_active) {
                if (manual_restore_requested) {
                    transition = DockingTransition::ManualRestore;
                } else if (preset_rebuild_requested) {
                    transition = DockingTransition::PresetRebuild;
                }
            }

            if (transition == DockingTransition::ManualRestore) {
                ImGui::LoadIniSettingsFromMemory(runtime.workspace_manual_docking_ini.c_str(), runtime.workspace_manual_docking_ini.size());
                runtime.workspace_manual_layout_pending_load = false;
                runtime.workspace_preset_apply_requested = false;
                runtime.workspace_dock_rebuild_requested = false;
                runtime.last_applied_workspace_mode = runtime.workspace_mode;
            }

            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

            if (transition == DockingTransition::PresetRebuild) {
                ImGui::ClearIniSettings();
                if (runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset) {
                    ApplyWorkspacePresetVisibility(runtime);
                }
                ApplyWorkspaceDockLayout(runtime, dockspace_id);
            }

            if (runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual && docking_dragging_active) {
                runtime.workspace_manual_layout_save_suppressed = false;
            }

            if (docking_dragging_active || transition != DockingTransition::None) {
                runtime.workspace_manual_layout_stable_frames = 0;
            } else {
                ++runtime.workspace_manual_layout_stable_frames;
            }
        } else {
            ImGui::PopStyleVar(3);
        }
        ImGui::End();

        ImGuiWindowFlags panel_flags = ImGuiWindowFlags_NoCollapse;

        if (ImGui::BeginMainMenuBar()) {
            RenderWorkspaceToolbar(runtime);
            ImGui::EndMainMenuBar();
        }

        if (runtime.show_overview_window) {
            if (ImGui::Begin(kOverviewWindowName, &runtime.show_overview_window, panel_flags)) {
                RenderRuntimeOverviewPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_editor_window) {
            if (ImGui::Begin(kEditorWindowName, &runtime.show_editor_window, panel_flags)) {
                RenderRuntimeEditorPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_timeline_window) {
            if (ImGui::Begin(kTimelineWindowName, &runtime.show_timeline_window, panel_flags)) {
                RenderRuntimeTimelinePanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_perception_window) {
            if (ImGui::Begin(kPerceptionWindowName, &runtime.show_perception_window, panel_flags)) {
                RenderRuntimePerceptionPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_mapping_window) {
            if (ImGui::Begin(kMappingWindowName, &runtime.show_mapping_window, panel_flags)) {
                RenderRuntimeMappingPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_asr_chat_window) {
            if (ImGui::Begin(kAsrChatWindowName, &runtime.show_asr_chat_window, panel_flags)) {
                RenderRuntimeAsrChatPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_error_window) {
            if (ImGui::Begin(kErrorWindowName, &runtime.show_error_window, panel_flags)) {
                RenderRuntimeErrorPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_ops_window) {
            if (ImGui::Begin(kOpsWindowName, &runtime.show_ops_window, panel_flags)) {
                RenderRuntimeOpsPanel(runtime);
            }
            ImGui::End();
        }

            const bool docking_window_drag_active =
                ImGui::IsDragDropActive() &&
                GImGui != nullptr &&
                GImGui->DragDropPayload.IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW);
            const bool docking_dragging_active =
                ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                docking_window_drag_active;
        constexpr int kManualLayoutSaveStableFrames = 12;
        if (!docking_dragging_active &&
            runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual &&
            !runtime.workspace_manual_layout_reset_requested &&
            !runtime.workspace_manual_layout_save_suppressed &&
            runtime.workspace_manual_layout_stable_frames >= kManualLayoutSaveStableFrames) {
            const std::string latest_docking_ini = ImGui::SaveIniSettingsToMemory();
            if (latest_docking_ini != runtime.workspace_manual_docking_ini) {
                runtime.workspace_manual_docking_ini = latest_docking_ini;
            }
        }

        if (runtime.show_inspector_window) {
            if (ImGui::Begin(kInspectorWindowName, &runtime.show_inspector_window, panel_flags)) {
                bridge.RenderResourceTreeInspector();
            }
            ImGui::End();
        }

        const bool reminder_open = runtime.workspace_mode != WorkspaceMode::Animation;
        if (runtime.show_reminder_window) {
            if (ImGui::Begin(kReminderWindowName, &runtime.show_reminder_window, panel_flags)) {
                if (!reminder_open) {
                    ImGui::TextDisabled("Workspace %s 默认弱化 Reminder 面板，可手动停靠到边缘区域。",
                                        WorkspaceModeName(runtime.workspace_mode));
                }
                RenderReminderPanel(runtime);
            }
            ImGui::End();
        }
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

        if (ImGui::BeginMainMenuBar()) {
            RenderWorkspaceToolbar(runtime);
            ImGui::EndMainMenuBar();
        }

        if (runtime.show_overview_window) {
            ImGui::SetNextWindowPos(ImVec2(base_x + margin, base_y + top_offset), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(debug_w, std::max(220.0f, debug_h - 104.0f)), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(kOverviewWindowName, &runtime.show_overview_window)) {
                RenderRuntimeOverviewPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_error_window) {
            ImGui::SetNextWindowPos(ImVec2(right_x, right_y), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(right_w, inspector_h), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(kErrorWindowName, &runtime.show_error_window)) {
                RenderRuntimeErrorPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.show_ops_window) {
            ImGui::SetNextWindowPos(ImVec2(right_x, right_y + inspector_h + gap), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(right_w, reminder_h), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(kOpsWindowName, &runtime.show_ops_window)) {
                RenderRuntimeOpsPanel(runtime);
            }
            ImGui::End();
        }
#endif
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), runtime.renderer);
    SDL_RenderPresent(runtime.renderer);
}

}  // namespace k2d
