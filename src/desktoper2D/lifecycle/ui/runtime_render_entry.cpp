#include "desktoper2D/lifecycle/ui/runtime_render_entry.h"

#include "app_debug_ui_internal.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "desktoper2D/controllers/window_controller.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"
#include "desktoper2D/lifecycle/ui/runtime_imgui_backend.h"
#include "desktoper2D/lifecycle/ui/reminder_panel.h"
#include "desktoper2D/rendering/app_renderer.h"

namespace desktoper2D {
namespace {

constexpr const char *kOverviewWindowName = "Runtime Overview";
constexpr const char *kEditorWindowName = "Runtime Editor";
constexpr const char *kTimelineWindowName = "Runtime Timeline";
constexpr const char *kPerceptionWindowName = "Runtime Perception";
constexpr const char *kOcrWindowName = "Runtime OCR";
constexpr const char *kMappingWindowName = "Runtime Mapping";
constexpr const char *kAsrWindowName = "Runtime ASR";
constexpr const char *kPluginWorkerWindowName = "Runtime Plugin Worker";
constexpr const char *kChatWindowName = "Runtime Chat";
constexpr const char *kErrorWindowName = "Runtime Health";
constexpr const char *kInspectorWindowName = "Model Hierarchy + Inspector";
constexpr const char *kReminderWindowName = "Reminder";
constexpr const char *kPluginQuickControlWindowName = "Plugin Quick Control";
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
bool IsWorkspaceDockingSectionHeader(const std::string &line) {
    return line == "[Docking][Data]";
}

std::string ExtractWorkspaceDockingIniSections(const std::string &full_ini) {
    std::string result;
    std::size_t pos = 0;
    while (pos < full_ini.size()) {
        const std::size_t line_end = full_ini.find('\n', pos);
        const std::size_t next = (line_end == std::string::npos) ? full_ini.size() : line_end + 1;
        const std::string line = full_ini.substr(pos, next - pos);
        const std::string line_no_eol = (!line.empty() && line.back() == '\n') ? line.substr(0, line.size() - 1) : line;

        if (!IsWorkspaceDockingSectionHeader(line_no_eol)) {
            pos = next;
            continue;
        }

        result += line;
        pos = next;

        while (pos < full_ini.size()) {
            const std::size_t body_line_end = full_ini.find('\n', pos);
            const std::size_t body_next = (body_line_end == std::string::npos) ? full_ini.size() : body_line_end + 1;
            const std::string body_line = full_ini.substr(pos, body_next - pos);
            const std::string body_line_no_eol = (!body_line.empty() && body_line.back() == '\n')
                ? body_line.substr(0, body_line.size() - 1)
                : body_line;
            if (!body_line_no_eol.empty() && body_line_no_eol.front() == '[') {
                break;
            }
            result += body_line;
            pos = body_next;
        }
        break;
    }
    return result;
}

void LoadWorkspaceDockingIni(const std::string &workspace_docking_ini) {
    if (workspace_docking_ini.empty()) {
        return;
    }
    ImGui::LoadIniSettingsFromMemory(workspace_docking_ini.c_str(), workspace_docking_ini.size());
}

std::string SaveWorkspaceDockingIni() {
    const std::string full_ini = ImGui::SaveIniSettingsToMemory();
    return ExtractWorkspaceDockingIniSections(full_ini);
}

bool IsWorkspaceDockTransactionActive() {
    return ImGui::IsDragDropActive() &&
           GImGui != nullptr &&
           GImGui->DragDropPayload.IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW);
}

void ApplyWorkspacePresetVisibility(AppRuntime &runtime) {
    ApplyWorkspaceWindowVisibility(runtime, BuildWorkspaceDefaultVisibility(runtime.workspace_ui.mode));
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

    enum class DockSlot {
        Main,
        Left,
        Right,
        Bottom,
        RightBottom,
    };
    enum class DockRecipeOpType {
        Split,
        DockWindow,
    };
    struct DockRecipeOp {
        DockRecipeOpType type = DockRecipeOpType::Split;
        DockSlot source = DockSlot::Main;
        ImGuiDir split_dir = ImGuiDir_Left;
        float split_ratio = 0.5f;
        DockSlot split_child = DockSlot::Left;
        DockSlot split_remain = DockSlot::Main;
        const char *window_name = nullptr;
        DockSlot dock_slot = DockSlot::Main;
    };
    struct DockRecipe {
        const DockRecipeOp *ops = nullptr;
        std::size_t count = 0;
    };

    static const DockRecipeOp kDebugRecipeOps[] = {
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Left, 0.24f, DockSlot::Left, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Right, 0.30f, DockSlot::Right, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Right, ImGuiDir_Down, 0.32f, DockSlot::RightBottom, DockSlot::Right},
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Down, 0.24f, DockSlot::Bottom, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Bottom, ImGuiDir_Down, 0.38f, DockSlot::RightBottom, DockSlot::Bottom},
        {DockRecipeOpType::Split, DockSlot::Right, ImGuiDir_Down, 0.45f, DockSlot::RightBottom, DockSlot::Right},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kOverviewWindowName, DockSlot::Left},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kErrorWindowName, DockSlot::Main},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kPerceptionWindowName, DockSlot::Right},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kOcrWindowName, DockSlot::RightBottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kAsrWindowName, DockSlot::Bottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kPluginWorkerWindowName, DockSlot::RightBottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kChatWindowName, DockSlot::Bottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kInspectorWindowName, DockSlot::Left},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kReminderWindowName, DockSlot::RightBottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kPluginQuickControlWindowName, DockSlot::RightBottom},
    };
    static const DockRecipeOp kPerceptionRecipeOps[] = {
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Left, 0.34f, DockSlot::Left, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Right, 0.30f, DockSlot::Right, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Down, 0.22f, DockSlot::Bottom, DockSlot::Main},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kPerceptionWindowName, DockSlot::Left},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kOverviewWindowName, DockSlot::Main},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kErrorWindowName, DockSlot::Right},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kReminderWindowName, DockSlot::Bottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kInspectorWindowName, DockSlot::Right},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kPluginQuickControlWindowName, DockSlot::RightBottom},
    };
    static const DockRecipeOp kAuthoringRecipeOps[] = {
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Left, 0.28f, DockSlot::Left, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Right, 0.24f, DockSlot::Right, DockSlot::Main},
        {DockRecipeOpType::Split, DockSlot::Main, ImGuiDir_Down, 0.22f, DockSlot::Bottom, DockSlot::Main},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kEditorWindowName, DockSlot::Left},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kTimelineWindowName, DockSlot::Main},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kMappingWindowName, DockSlot::Right},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kOverviewWindowName, DockSlot::Bottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kInspectorWindowName, DockSlot::Right},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kReminderWindowName, DockSlot::Bottom},
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kPerceptionWindowName, DockSlot::Left},
    };
    static const DockRecipeOp kDefaultRecipeOps[] = {
        {DockRecipeOpType::DockWindow, DockSlot::Main, ImGuiDir_Left, 0.0f, DockSlot::Left, DockSlot::Main, kOverviewWindowName, DockSlot::Main},
    };

    auto get_recipe = [&](WorkspaceMode mode) -> DockRecipe {
        switch (mode) {
            case WorkspaceMode::Debug: return DockRecipe{.ops = kDebugRecipeOps, .count = std::size(kDebugRecipeOps)};
            case WorkspaceMode::Perception: return DockRecipe{.ops = kPerceptionRecipeOps, .count = std::size(kPerceptionRecipeOps)};
            case WorkspaceMode::Animation:
            case WorkspaceMode::Authoring: return DockRecipe{.ops = kAuthoringRecipeOps, .count = std::size(kAuthoringRecipeOps)};
            default: return DockRecipe{.ops = kDefaultRecipeOps, .count = std::size(kDefaultRecipeOps)};
        }
    };

    ImGuiID slot_ids[5] = {};
    slot_ids[static_cast<int>(DockSlot::Main)] = dock_main;
    slot_ids[static_cast<int>(DockSlot::Left)] = dock_left;
    slot_ids[static_cast<int>(DockSlot::Right)] = dock_right;
    slot_ids[static_cast<int>(DockSlot::Bottom)] = dock_bottom;
    slot_ids[static_cast<int>(DockSlot::RightBottom)] = dock_right_bottom;

    const DockRecipe recipe = get_recipe(runtime.workspace_ui.mode);
    for (std::size_t i = 0; i < recipe.count; ++i) {
        const DockRecipeOp &op = recipe.ops[i];
        if (op.type == DockRecipeOpType::Split) {
            ImGui::DockBuilderSplitNode(slot_ids[static_cast<int>(op.source)],
                                        op.split_dir,
                                        op.split_ratio,
                                        &slot_ids[static_cast<int>(op.split_child)],
                                        &slot_ids[static_cast<int>(op.split_remain)]);
        } else if (op.type == DockRecipeOpType::DockWindow && op.window_name != nullptr) {
            ImGui::DockBuilderDockWindow(op.window_name, slot_ids[static_cast<int>(op.dock_slot)]);
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    // Preset 仅负责 Dock 分配，不在每次重建时覆盖窗口可见性开关。
    runtime.workspace_ui.panels.last_applied_mode = runtime.workspace_ui.mode;
    runtime.workspace_ui.panels.preset_apply_requested = false;
    runtime.workspace_ui.dock_rebuild_requested = false;
    runtime.workspace_ui.panels.manual_layout_reset_requested = false;
}
#endif

}  // namespace

void RunRuntimeRenderEntry(AppRuntime &runtime, const RuntimeRenderBridge &bridge) {
    BeginRuntimeImGuiFrame();
    ImGui::NewFrame();

    RenderAppFrame(AppRenderContext{
        .window_state = &runtime.window_state,
        .model_loaded = runtime.model_loaded,
        .model = &runtime.model,
        .show_debug_stats = runtime.show_debug_stats,
        .manual_param_mode = runtime.manual_param_mode,
        .selected_param_index = runtime.selected_param_index,
        .edit_mode = runtime.edit_mode,
        .selected_part_index = runtime.selected_part_index,
        .gizmo_hover_handle = runtime.gizmo_hover_handle,
        .gizmo_active_handle = runtime.gizmo_active_handle,
        .editor_status = runtime.editor_status.c_str(),
        .editor_status_ttl = runtime.editor_status_ttl,
        .debug_fps = runtime.debug_fps,
        .debug_frame_ms = runtime.debug_frame_ms,
        .has_model_parts = bridge.has_model_parts,
        .has_model_params = bridge.has_model_params,
        .ensure_selected_part_index_valid = bridge.ensure_selected_part_index_valid,
        .ensure_selected_param_index_valid = bridge.ensure_selected_param_index_valid,
        .compute_part_aabb = bridge.compute_part_aabb,
    });

    const ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantCaptureKeyboard) {
        runtime.running = false;
    }

    if (runtime.gui_enabled && runtime.workspace_ui.panels.show_workspace_window) {
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

            const bool docking_dragging_active = IsWorkspaceDockTransactionActive();

            enum class DockingTransition {
                None,
                ManualRestore,
                PresetRebuild,
            };

            const bool manual_restore_requested =
                runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual &&
                runtime.workspace_ui.panels.manual_layout_pending_load &&
                !runtime.workspace_ui.panels.manual_docking_ini.empty();
            const bool manual_restore_fallback_requested =
                runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual &&
                runtime.workspace_ui.panels.manual_layout_pending_load &&
                runtime.workspace_ui.panels.manual_docking_ini.empty();

            const bool preset_rebuild_requested =
                runtime.workspace_ui.dock_rebuild_requested ||
                (runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Preset &&
                 (runtime.workspace_ui.panels.preset_apply_requested ||
                  runtime.workspace_ui.panels.last_applied_mode != runtime.workspace_ui.mode));

            // 单向状态机：同帧最多发生一种转移，优先 ManualRestore，再 PresetRebuild。
            DockingTransition transition = DockingTransition::None;
            if (!docking_dragging_active) {
                if (manual_restore_requested) {
                    transition = DockingTransition::ManualRestore;
                } else if (manual_restore_fallback_requested || preset_rebuild_requested) {
                    transition = DockingTransition::PresetRebuild;
                }
            }

            if (transition == DockingTransition::ManualRestore) {
                LoadWorkspaceDockingIni(runtime.workspace_ui.panels.manual_docking_ini);
                runtime.workspace_ui.panels.manual_layout_pending_load = false;
                runtime.workspace_ui.panels.preset_apply_requested = false;
                runtime.workspace_ui.dock_rebuild_requested = false;
                runtime.workspace_ui.panels.last_applied_mode = runtime.workspace_ui.mode;
            }

            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

            if (transition == DockingTransition::PresetRebuild) {
                if (runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Preset || manual_restore_fallback_requested) {
                    // 兜底窗口集合复用按 workspace_mode 的默认可见性策略。
                    ApplyWorkspacePresetVisibility(runtime);
                }
                // 仅重建当前 Workspace DockSpace 节点，保留其他窗口/工作区 ini 状态。
                ApplyWorkspaceDockLayout(runtime, dockspace_id);
                if (manual_restore_fallback_requested) {
                    runtime.workspace_ui.panels.manual_layout_pending_load = false;
                    runtime.workspace_ui.panels.preset_apply_requested = false;
                    runtime.workspace_ui.dock_rebuild_requested = false;
                    runtime.workspace_ui.panels.last_applied_mode = runtime.workspace_ui.mode;
                }
            }

            if (runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual && docking_dragging_active) {
                runtime.workspace_ui.panels.manual_layout_save_suppressed = false;
            }

            if (docking_dragging_active || transition != DockingTransition::None) {
                runtime.workspace_ui.panels.manual_layout_stable_frames = 0;
            } else {
                ++runtime.workspace_ui.panels.manual_layout_stable_frames;
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

        if (runtime.workspace_ui.panels.show_overview_window) {
            if (ImGui::Begin(kOverviewWindowName, &runtime.workspace_ui.panels.show_overview_window, panel_flags)) {
                RenderRuntimeOverviewPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_editor_window) {
            if (ImGui::Begin(kEditorWindowName, &runtime.workspace_ui.panels.show_editor_window, panel_flags)) {
                RenderRuntimeEditorPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_timeline_window) {
            if (ImGui::Begin(kTimelineWindowName, &runtime.workspace_ui.panels.show_timeline_window, panel_flags)) {
                RenderRuntimeTimelinePanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_perception_window) {
            if (ImGui::Begin(kPerceptionWindowName, &runtime.workspace_ui.panels.show_perception_window, panel_flags)) {
                RenderRuntimePerceptionPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_ocr_window) {
            if (ImGui::Begin(kOcrWindowName, &runtime.workspace_ui.panels.show_ocr_window, panel_flags)) {
                RenderRuntimeOcrPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_mapping_window) {
            if (ImGui::Begin(kMappingWindowName, &runtime.workspace_ui.panels.show_mapping_window, panel_flags)) {
                RenderRuntimeMappingPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_asr_chat_window) {
            if (ImGui::Begin(kAsrWindowName, &runtime.workspace_ui.panels.show_asr_chat_window, panel_flags)) {
                RenderRuntimeAsrPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_plugin_worker_window) {
            if (ImGui::Begin(kPluginWorkerWindowName, &runtime.workspace_ui.panels.show_plugin_worker_window, panel_flags)) {
                RenderRuntimePluginWorkerPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_chat_window) {
            if (ImGui::Begin(kChatWindowName, &runtime.workspace_ui.panels.show_chat_window, panel_flags)) {
                RenderRuntimeChatPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_error_window) {
            if (ImGui::Begin(kErrorWindowName, &runtime.workspace_ui.panels.show_error_window, panel_flags)) {
                RenderRuntimeErrorPanel(runtime);
            }
            ImGui::End();
        }
  
        if (runtime.workspace_ui.panels.show_plugin_quick_control_window) {
            if (ImGui::Begin(kPluginQuickControlWindowName, &runtime.workspace_ui.panels.show_plugin_quick_control_window, panel_flags)) {
                RenderRuntimePluginQuickControlPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_plugin_detail_window) {
            const ImGuiViewport *detail_vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(detail_vp->WorkPos);
            ImGui::SetNextWindowSize(detail_vp->WorkSize);
            ImGuiWindowFlags detail_flags = ImGuiWindowFlags_NoDocking |
                                            ImGuiWindowFlags_NoCollapse |
                                            ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_NoMove;
            if (ImGui::Begin("Plugin Detail", &runtime.workspace_ui.panels.show_plugin_detail_window, detail_flags)) {
                RenderRuntimePluginDetailPanel(runtime);
            }
            ImGui::End();
        }
 
        const bool docking_window_drag_active =
            ImGui::IsDragDropActive() &&
            GImGui != nullptr &&
            GImGui->DragDropPayload.IsDataType(IMGUI_PAYLOAD_TYPE_WINDOW);
        const bool docking_dragging_active = docking_window_drag_active;
        constexpr int kManualLayoutSaveStableFrames = 12;
        if (!docking_dragging_active &&
            runtime.workspace_ui.panels.layout_mode == WorkspaceLayoutMode::Manual &&
            !runtime.workspace_ui.panels.manual_layout_reset_requested &&
            !runtime.workspace_ui.panels.manual_layout_save_suppressed &&
            runtime.workspace_ui.panels.manual_layout_stable_frames >= kManualLayoutSaveStableFrames) {
            const std::string latest_docking_ini = SaveWorkspaceDockingIni();
            if (!latest_docking_ini.empty() && latest_docking_ini != runtime.workspace_ui.panels.manual_docking_ini) {
                runtime.workspace_ui.panels.manual_docking_ini = latest_docking_ini;
            }
        }

        if (runtime.workspace_ui.panels.show_inspector_window) {
            if (ImGui::Begin(kInspectorWindowName, &runtime.workspace_ui.panels.show_inspector_window, panel_flags)) {
                bridge.RenderResourceTreeInspector();
            }
            ImGui::End();
        }

        const bool reminder_open = runtime.workspace_ui.mode != WorkspaceMode::Animation;
        if (runtime.workspace_ui.panels.show_reminder_window) {
            if (ImGui::Begin(kReminderWindowName, &runtime.workspace_ui.panels.show_reminder_window, panel_flags)) {
                if (!reminder_open) {
                    ImGui::TextDisabled("Workspace %s 默认弱化 Reminder 面板，可手动停靠到边缘区域。",
                                        WorkspaceModeName(runtime.workspace_ui.mode));
                }
                RenderReminderPanel(runtime);
            }
            ImGui::End();
        }
#else
        const float base_x = vp ? vp->WorkPos.x : 0.0f;
        const float base_y = vp ? vp->WorkPos.y : 0.0f;
        const float work_w = vp ? vp->WorkSize.x : static_cast<float>(runtime.window_state.window_w);
        const float work_h = vp ? vp->WorkSize.y : static_cast<float>(runtime.window_state.window_h);

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

        if (runtime.workspace_ui.panels.show_overview_window) {
            ImGui::SetNextWindowPos(ImVec2(base_x + margin, base_y + top_offset), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(debug_w, std::max(220.0f, debug_h - 104.0f)), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(kOverviewWindowName, &runtime.workspace_ui.panels.show_overview_window)) {
                RenderRuntimeOverviewPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_error_window) {
            ImGui::SetNextWindowPos(ImVec2(right_x, right_y), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(right_w, inspector_h), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(kErrorWindowName, &runtime.workspace_ui.panels.show_error_window)) {
                RenderRuntimeErrorPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_plugin_quick_control_window) {
            ImGui::SetNextWindowPos(ImVec2(right_x, right_y + inspector_h + gap), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(right_w, reminder_h), ImGuiCond_FirstUseEver);
            if (ImGui::Begin(kPluginQuickControlWindowName, &runtime.workspace_ui.panels.show_plugin_quick_control_window)) {
                RenderRuntimePluginQuickControlPanel(runtime);
            }
            ImGui::End();
        }

        if (runtime.workspace_ui.panels.show_plugin_detail_window) {
            ImGui::SetNextWindowPos(ImVec2(base_x + margin, base_y + top_offset), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(usable_w, usable_h), ImGuiCond_FirstUseEver);
            ImGuiWindowFlags detail_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            if (ImGui::Begin("Plugin Detail", &runtime.workspace_ui.panels.show_plugin_detail_window, detail_flags)) {
                RenderRuntimePluginDetailPanel(runtime);
            }
            ImGui::End();
        }
#endif
    }

    ImGui::Render();
    RenderRuntimeImGuiDrawData(runtime.window_state);
    PresentWindowFrame(runtime.window_state);
}

}  // namespace desktoper2D
