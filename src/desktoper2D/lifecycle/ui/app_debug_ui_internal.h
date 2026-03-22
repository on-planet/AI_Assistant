#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL_iostream.h>

#include "imgui.h"

#include "desktoper2D/core/json.h"
#include "desktoper2D/editor/editor_commands.h"
#include "desktoper2D/lifecycle/editor/editor_session_service.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"
#include "desktoper2D/lifecycle/ui/ui_empty_state.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_types.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_runtime_actions.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_presenter.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_editor_service.h"

namespace desktoper2D {

using ParamGroup = std::pair<std::string, std::vector<int>>;

struct WorkspaceWindowVisibility {
    bool show_workspace_window = true;
    bool show_overview_window = true;
    bool show_editor_window = true;
    bool show_timeline_window = true;
    bool show_perception_window = true;
    bool show_ocr_window = true;
    bool show_mapping_window = true;
    bool show_asr_chat_window = true;
    bool show_plugin_worker_window = true;
    bool show_chat_window = true;
    bool show_error_window = true;
    bool show_inspector_window = true;
    bool show_reminder_window = true;
    bool show_plugin_quick_control_window = true;
    bool show_plugin_detail_window = false;
};

inline WorkspaceWindowVisibility BuildWorkspaceDefaultVisibility(const WorkspaceMode mode) {
    WorkspaceWindowVisibility v{};
    switch (mode) {
        case WorkspaceMode::Debug:
            v.show_editor_window = false;
            v.show_timeline_window = false;
            v.show_mapping_window = false;
            break;
        case WorkspaceMode::Perception:
            v.show_editor_window = false;
            v.show_timeline_window = false;
            v.show_mapping_window = false;
            v.show_ocr_window = false;
            v.show_asr_chat_window = false;
            v.show_plugin_worker_window = false;
            v.show_chat_window = false;
            v.show_plugin_detail_window = false;
            break;
        case WorkspaceMode::Animation:
            v.show_error_window = false;
            v.show_ocr_window = false;
            v.show_asr_chat_window = false;
            v.show_plugin_worker_window = false;
            v.show_chat_window = false;
            v.show_reminder_window = false;
            v.show_plugin_quick_control_window = false;
            v.show_plugin_detail_window = false;
            break;
        case WorkspaceMode::Authoring:
            v.show_ocr_window = false;
            v.show_asr_chat_window = false;
            v.show_plugin_worker_window = false;
            v.show_chat_window = false;
            v.show_plugin_detail_window = false;
            break;
        default:
            break;
    }
    return v;
}

inline void ApplyWorkspaceWindowVisibility(AppRuntime &runtime, const WorkspaceWindowVisibility &v) {
    runtime.workspace_ui.panels.show_workspace_window = v.show_workspace_window;
    runtime.workspace_ui.panels.show_overview_window = v.show_overview_window;
    runtime.workspace_ui.panels.show_editor_window = v.show_editor_window;
    runtime.workspace_ui.panels.show_timeline_window = v.show_timeline_window;
    runtime.workspace_ui.panels.show_perception_window = v.show_perception_window;
    runtime.workspace_ui.panels.show_ocr_window = v.show_ocr_window;
    runtime.workspace_ui.panels.show_mapping_window = v.show_mapping_window;
    runtime.workspace_ui.panels.show_asr_chat_window = v.show_asr_chat_window;
    runtime.workspace_ui.panels.show_plugin_worker_window = v.show_plugin_worker_window;
    runtime.workspace_ui.panels.show_chat_window = v.show_chat_window;
    runtime.workspace_ui.panels.show_error_window = v.show_error_window;
    runtime.workspace_ui.panels.show_inspector_window = v.show_inspector_window;
    runtime.workspace_ui.panels.show_reminder_window = v.show_reminder_window;
    runtime.workspace_ui.panels.show_plugin_quick_control_window = v.show_plugin_quick_control_window;
    runtime.workspace_ui.panels.show_plugin_detail_window = v.show_plugin_detail_window;
}

inline bool HasAnyWorkspaceChildWindowVisible(const AppRuntime &runtime) {
    return runtime.workspace_ui.panels.show_overview_window ||
           runtime.workspace_ui.panels.show_editor_window ||
           runtime.workspace_ui.panels.show_timeline_window ||
           runtime.workspace_ui.panels.show_perception_window ||
           runtime.workspace_ui.panels.show_ocr_window ||
           runtime.workspace_ui.panels.show_mapping_window ||
           runtime.workspace_ui.panels.show_asr_chat_window ||
           runtime.workspace_ui.panels.show_plugin_worker_window ||
           runtime.workspace_ui.panels.show_chat_window ||
           runtime.workspace_ui.panels.show_error_window ||
           runtime.workspace_ui.panels.show_inspector_window ||
           runtime.workspace_ui.panels.show_reminder_window ||
           runtime.workspace_ui.panels.show_plugin_quick_control_window ||
           runtime.workspace_ui.panels.show_plugin_detail_window;
}

struct TimelineInteractionStorage {
    std::vector<TimelineKeyframe> drag_undo_snapshot;
    std::vector<std::uint64_t> drag_undo_selected_ids;
    bool drag_snapshot_captured = false;
    int dragging_keyframe_index = -1;
    int dragging_channel_index = -1;

    bool box_select_active = false;
    float box_select_start_x = 0.0f;
    float box_select_start_y = 0.0f;
    float box_select_end_x = 0.0f;
    float box_select_end_y = 0.0f;
};

TimelineInteractionStorage &GetTimelineInteractionStorage();

const char *TaskPrimaryCategoryNameUi(TaskPrimaryCategory c);
const char *TaskSecondaryCategoryNameUi(TaskSecondaryCategory c);

void RenderModuleLatencyPanel(const AppRuntime &runtime);
void RenderRuntimeErrorClassificationTable(const AppRuntime &runtime, ErrorViewFilter filter);
std::string DetectParamPrefix(const std::string &param_id);
std::string DetectParamSemanticGroup(const std::string &param_id);
bool ParamMatchesSearch(const std::string &param_id, const char *search_text);
std::vector<ParamGroup> BuildParamGroups(const AppRuntime &runtime, int group_mode, const char *search_text);
std::vector<EditorBatchBindTemplateItem> BuildEditorBatchBindTemplates();
std::pair<float, float> BatchBindOutRangeFor(BindingType type);
EditorBatchBindValidation ValidateBatchBind(const std::vector<EditorParamRowState> &param_rows,
                                            BindingType type,
                                            float in_min,
                                            float in_max,
                                            float out_min,
                                            float out_max);
void RenderUnifiedPluginStatusCard(const AppRuntime &runtime, const char *empty_hint);
void RenderRuntimePluginHealthPanel(AppRuntime &runtime);
std::string &RuntimeOpsStatusStorage();
void RenderRuntimeOpsActions(AppRuntime &runtime);
void RenderRuntimePluginManagement(AppRuntime &runtime);

}  // namespace desktoper2D
