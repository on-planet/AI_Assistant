#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_types.h"

struct UnifiedPluginEntry;

namespace desktoper2D {

const char *UnifiedPluginStatusLabel(UnifiedPluginStatus status);
ImVec4 UnifiedPluginStatusColor(UnifiedPluginStatus status);
const char *UnifiedPluginKindLabel(UnifiedPluginKind kind);
std::string JoinAssetsUi(const std::vector<std::string> &assets);
std::vector<std::string> SplitCsvUi(const char *csv);

void RenderRuntimeErrorInfo(const char *label, const RuntimeErrorInfo &err);
void RenderModuleLatestErrorCard(const std::string &err);
void RenderOverviewTableRow(const char *label, const char *value);
void RenderHealthLampRow(const char *label, HealthState state, const char *detail);
void RenderModuleLatencyRow(const char *label,
                            double last_ms,
                            double avg_ms,
                            double p95_ms,
                            const char *detail);
std::string LimitTextLines(const std::string &text, int max_lines, bool *out_truncated = nullptr);
void RenderLongTextBlock(const char *title, const char *child_id, std::string *text, int max_lines, float child_h = 90.0f);
bool RenderFilePickerButton(const char *button_label,
                            std::string *out_path,
                            const char *filter_name,
                            const char *filter_spec,
                            const char *default_path = nullptr,
                            std::string *out_error = nullptr);

}  // namespace desktoper2D
