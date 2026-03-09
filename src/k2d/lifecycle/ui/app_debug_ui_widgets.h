#pragma once

#include <string>

#include "imgui.h"

#include "k2d/lifecycle/ui/app_debug_ui_types.h"

namespace k2d {

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

}  // namespace k2d
