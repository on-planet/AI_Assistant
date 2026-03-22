#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

namespace {

const char *ToLogLevelLabel(PluginLogLevel level) {
    switch (level) {
        case PluginLogLevel::Info:
            return "info";
        case PluginLogLevel::Warning:
            return "warn";
        case PluginLogLevel::Error:
            return "error";
        default:
            return "unknown";
    }
}

}

void RenderModuleLatencyPanel(const AppRuntime &runtime) {
    ImGui::SeparatorText("Module Latency");
    if (ImGui::BeginTable("module_latency_table",
                          5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Last(ms)", ImGuiTableColumnFlags_WidthStretch, 0.14f);
        ImGui::TableSetupColumn("Avg(ms)", ImGuiTableColumnFlags_WidthStretch, 0.14f);
        ImGui::TableSetupColumn("P95(ms)", ImGuiTableColumnFlags_WidthStretch, 0.14f);
        ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableHeadersRow();

        RenderModuleLatencyRow("Frame",
                               runtime.debug_frame_ms,
                               runtime.debug_frame_ms,
                               runtime.debug_frame_ms,
                               "main loop");
        RenderModuleLatencyRow("Perception.Scene",
                               runtime.perception_state.scene_avg_latency_ms,
                               runtime.perception_state.scene_avg_latency_ms,
                               runtime.observability.scene_p95_latency_ms,
                               "scene classifier");
        RenderModuleLatencyRow("Perception.OCR",
                               runtime.perception_state.ocr_avg_latency_ms,
                               runtime.perception_state.ocr_avg_latency_ms,
                               runtime.observability.ocr_p95_latency_ms,
                               runtime.perception_state.ocr_skipped_due_timeout ? "timeout skipped" : "det+rec pipeline");
        RenderModuleLatencyRow("Perception.FaceMesh",
                               runtime.perception_state.face_avg_latency_ms,
                               runtime.perception_state.face_avg_latency_ms,
                               runtime.observability.face_p95_latency_ms,
                               "camera facemesh");
        RenderModuleLatencyRow("ASR",
                               static_cast<double>(runtime.asr_last_result.latency_ms),
                               static_cast<double>(runtime.asr_infer_total_sec * 1000.0 /
                                                   std::max<std::int64_t>(1, runtime.asr_total_segments)),
                               static_cast<double>(runtime.asr_last_result.latency_ms),
                               runtime.asr_last_switch_reason.empty() ? "provider route" : runtime.asr_last_switch_reason.c_str());
        RenderModuleLatencyRow("Plugin.Worker",
                               runtime.plugin.last_latency_ms,
                               runtime.plugin.avg_latency_ms,
                               runtime.plugin.latency_p95_ms,
                               runtime.plugin.auto_disabled ? "auto disabled" : "worker stats");

        ImGui::EndTable();
    }
}

void RenderRuntimeErrorClassificationTable(const AppRuntime &runtime, ErrorViewFilter filter) {

    static int error_filter_idx = 1; // 默认 Non-OK
    const char *filters[] = {"All", "Non-OK", "Failed", "Degraded"};
    error_filter_idx = std::clamp(error_filter_idx, 0, 3);

    std::vector<RuntimeErrorRow> rows = BuildRuntimeErrorRows(runtime);
    std::stable_sort(rows.begin(), rows.end(), [](const RuntimeErrorRow &a, const RuntimeErrorRow &b) {
        if (a.info->count != b.info->count) {
            return a.info->count > b.info->count;
        }
        return a.recent_seq < b.recent_seq;
    });

    auto pass_filter = [filter](const RuntimeErrorRow &row) {
        switch (filter) {
            case ErrorViewFilter::All:
                return true;
            case ErrorViewFilter::NonOk:
                return row.info->code != RuntimeErrorCode::Ok;
            case ErrorViewFilter::Failed:
                return row.state == RuntimeModuleState::Failed;
            case ErrorViewFilter::Degraded:
                return row.state == RuntimeModuleState::Degraded || row.state == RuntimeModuleState::Recovering;
            default:
                return true;
        }
    };

    std::vector<RuntimeErrorRow> filtered_rows;
    filtered_rows.reserve(rows.size());
    for (const auto &row : rows) {
        if (pass_filter(row)) {
            filtered_rows.push_back(row);
        }
    }

    std::string all_errors;
    all_errors.reserve(1024);
    for (const auto &row : filtered_rows) {
        all_errors += row.label;
        all_errors += " | count=";
        all_errors += std::to_string(static_cast<long long>(row.info->count));
        all_errors += " | degraded=";
        all_errors += std::to_string(static_cast<long long>(row.info->degraded_count));
        all_errors += " | recent_seq=";
        all_errors += std::to_string(row.recent_seq);
        all_errors += " | ";
        all_errors += RuntimeErrorDomainName(row.info->domain);
        all_errors += ".";
        all_errors += RuntimeErrorCodeName(row.info->code);
        if (!row.info->detail.empty()) {
            all_errors += " | detail=";
            all_errors += row.info->detail;
        }
        all_errors += "\n";
    }

    if (filtered_rows.empty()) {
        RenderUnifiedEmptyState("runtime_error_empty_state",
                                "无错误",
                                "当前过滤条件下没有错误记录，系统可能处于健康状态。",
                                ImVec4(0.45f, 0.85f, 0.45f, 1.0f));
        return;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("##error_filter", &error_filter_idx, filters, 4);

    if (ImGui::BeginTable("runtime_error_table", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch, 0.24f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 0.14f);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch, 0.08f);
        ImGui::TableSetupColumn("Degraded", ImGuiTableColumnFlags_WidthStretch, 0.10f);
        ImGui::TableSetupColumn("Recent", ImGuiTableColumnFlags_WidthStretch, 0.08f);
        ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableHeadersRow();

        for (const auto &row : filtered_rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(RuntimeModuleStateColor(row.state), "%s", RuntimeModuleStateName(row.state));

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%lld", static_cast<long long>(row.info->count));

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%lld", static_cast<long long>(row.info->degraded_count));

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("T-%d", row.recent_seq);

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%s.%s", RuntimeErrorDomainName(row.info->domain), RuntimeErrorCodeName(row.info->code));

            ImGui::TableSetColumnIndex(6);
            if (row.info->detail.empty()) {
                ImGui::TextUnformatted("(none)");
            } else {
                ImGui::TextWrapped("%s", row.info->detail.c_str());
            }
        }

        const UnifiedPluginEntry *selected_plugin = nullptr;
        if (runtime.plugin.unified_selected_index >= 0 &&
            runtime.plugin.unified_selected_index < static_cast<int>(runtime.plugin.unified_entries.size())) {
            selected_plugin = &runtime.plugin.unified_entries[static_cast<std::size_t>(runtime.plugin.unified_selected_index)];
        }
        if (selected_plugin != nullptr) {
            auto it = runtime.plugin.logs.find(selected_plugin->id);
            if (it != runtime.plugin.logs.end()) {
                for (const auto &log : it->second) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("plugin.log");

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(ToLogLevelLabel(log.level));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", log.error_code);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted("-");

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%lld", static_cast<long long>(log.ts_ms));

                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(selected_plugin->id.c_str());

                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextWrapped("%s", log.message.c_str());
                }
            }
        }
        ImGui::EndTable();
    }
}

}  // namespace desktoper2D
