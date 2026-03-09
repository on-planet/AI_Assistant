#include "k2d/lifecycle/ui/app_debug_ui_widgets.h"

#include "k2d/lifecycle/ui/app_debug_ui_types.h"

namespace k2d {

void RenderRuntimeErrorInfo(const char *label, const RuntimeErrorInfo &err) {
    if (err.code == RuntimeErrorCode::Ok) {
        ImGui::Text("%s: OK", label);
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                       "%s: %s.%s (#%lld)",
                       label,
                       RuntimeErrorDomainName(err.domain),
                       RuntimeErrorCodeName(err.code),
                       static_cast<long long>(err.count));
    if (!err.detail.empty()) {
        ImGui::TextWrapped("  detail: %s", err.detail.c_str());
    }
}

void RenderModuleLatestErrorCard(const std::string &err) {
    ImGui::SeparatorText("Recent Error (Highlight)");
    if (err.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "None");
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ERROR");
    ImGui::TextWrapped("%s", err.c_str());
}

void RenderOverviewTableRow(const char *label, const char *value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

void RenderHealthLampRow(const char *label, HealthState state, const char *detail) {
    const ImVec4 green(0.35f, 0.9f, 0.45f, 1.0f);
    const ImVec4 yellow(1.0f, 0.82f, 0.25f, 1.0f);
    const ImVec4 red(1.0f, 0.35f, 0.35f, 1.0f);
    const ImVec4 color = state == HealthState::Green ? green : (state == HealthState::Yellow ? yellow : red);
    const char *state_name = state == HealthState::Green ? "GREEN" : (state == HealthState::Yellow ? "YELLOW" : "RED");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(color,
                       "%s%s%s",
                       state_name,
                       (detail != nullptr && detail[0] != '\0') ? " - " : "",
                       detail != nullptr ? detail : "");
}

void RenderModuleLatencyRow(const char *label,
                            double last_ms,
                            double avg_ms,
                            double p95_ms,
                            const char *detail) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%.1f", last_ms);
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%.1f", avg_ms);
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%.1f", p95_ms);
    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(detail ? detail : "");
}

std::string LimitTextLines(const std::string &text, int max_lines, bool *out_truncated) {
    if (out_truncated != nullptr) {
        *out_truncated = false;
    }
    if (max_lines <= 0 || text.empty()) {
        return text;
    }

    int lines = 1;
    std::size_t cut_pos = std::string::npos;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            lines += 1;
            if (lines > max_lines) {
                cut_pos = i;
                break;
            }
        }
    }
    if (cut_pos == std::string::npos) {
        return text;
    }
    if (out_truncated != nullptr) {
        *out_truncated = true;
    }
    return text.substr(0, cut_pos);
}

void RenderLongTextBlock(const char *title, const char *child_id, std::string *text, int max_lines, float child_h) {
    ImGui::SeparatorText(title);
    if (ImGui::Button((std::string("Copy##") + child_id).c_str())) {
        ImGui::SetClipboardText(text->c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string("Clear##") + child_id).c_str())) {
        text->clear();
    }

    bool truncated = false;
    const std::string display = LimitTextLines(*text, max_lines, &truncated);
    ImGui::BeginChild(child_id, ImVec2(-1.0f, child_h), ImGuiChildFlags_Borders);
    if (display.empty()) {
        ImGui::TextUnformatted("(empty)");
    } else {
        ImGui::TextWrapped("%s", display.c_str());
        if (truncated) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "... truncated to max %d lines", max_lines);
        }
    }
    ImGui::EndChild();
}

}  // namespace k2d
