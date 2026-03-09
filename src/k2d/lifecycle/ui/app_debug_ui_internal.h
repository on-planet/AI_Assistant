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

#include "k2d/core/json.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/lifecycle/editor/editor_session_service.h"
#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/ui/ui_empty_state.h"
#include "k2d/lifecycle/ui/app_debug_ui_types.h"
#include "k2d/lifecycle/ui/app_debug_ui_widgets.h"
#include "k2d/lifecycle/ui/app_debug_ui_presenter.h"
#include "k2d/lifecycle/ui/app_debug_ui_editor_service.h"

namespace k2d {

using ParamGroup = std::pair<std::string, std::vector<int>>;

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
void ResetPerceptionRuntimeState(PerceptionPipelineState &state);
void ResetAllRuntimeErrorCounters(AppRuntime &runtime);
bool ExportRuntimeSnapshotJson(const AppRuntime &runtime, const char *path, std::string *out_error);
void TriggerSingleStepSampling(AppRuntime &runtime);
std::string DetectParamPrefix(const std::string &param_id);
std::string DetectParamSemanticGroup(const std::string &param_id);
bool ParamMatchesSearch(const std::string &param_id, const char *search_text);
std::vector<ParamGroup> BuildParamGroups(const AppRuntime &runtime, int group_mode, const char *search_text);
void RenderOverviewRuntimeHealth(const AppRuntime &runtime);
std::string &RuntimeOpsStatusStorage();

}  // namespace k2d
