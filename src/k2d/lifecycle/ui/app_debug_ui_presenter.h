#pragma once

#include <array>
#include <string>
#include <vector>

#include "k2d/core/json.h"
#include "k2d/lifecycle/ui/app_debug_ui_types.h"

namespace k2d {

struct OverviewReadModel {
    float frame_ms = 0.0f;
    float fps = 0.0f;
    int model_parts = 0;
    bool model_loaded = false;
    bool plugin_degraded = false;
    double plugin_timeout_rate = 0.0;
    bool asr_available = false;
    bool chat_available = false;
    RuntimeErrorDomain recent_error_domain = RuntimeErrorDomain::None;
    std::string recent_error_text;
    const char *task_primary = "unknown";
    const char *task_secondary = "unknown";
};

struct WorkspaceReadModel {
    WorkspaceMode workspace_mode = WorkspaceMode::Animation;
    WorkspaceLayoutMode layout_mode = WorkspaceLayoutMode::Preset;
    const char *workspace_label = "Animation";
    const char *layout_label = "Preset";
    bool dock_rebuild_requested = false;
    bool preset_apply_requested = false;
};

struct OpsReadModel {
    bool running = true;
    std::string runtime_ops_status;
};

RuntimeErrorDomain PickRecentErrorDomain(const AppRuntime &runtime);
std::string PickRecentErrorText(const AppRuntime &runtime);
const char *RuntimeModuleStateName(RuntimeModuleState s);
ImVec4 RuntimeModuleStateColor(RuntimeModuleState s);
RuntimeModuleState ClassifyModuleState(const RuntimeErrorInfo &err,
                                       bool ready,
                                       bool degraded_hint,
                                       bool recovering_hint);
std::vector<RuntimeErrorRow> BuildRuntimeErrorRows(const AppRuntime &runtime);
JsonValue BuildRuntimeSnapshotJson(const AppRuntime &runtime);

OverviewReadModel BuildOverviewReadModel(const AppRuntime &runtime);
WorkspaceReadModel BuildWorkspaceReadModel(const AppRuntime &runtime);
OpsReadModel BuildOpsReadModel(const AppRuntime &runtime, const std::string &runtime_ops_status);

}  // namespace k2d
