#include "desktoper2D/lifecycle/ui/app_debug_ui_widgets.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

#include "desktoper2D/lifecycle/ui/app_debug_ui_types.h"

#include "nfd.h"

namespace desktoper2D {

namespace {

bool EnsureNfdInitialized(std::string *out_error) {
    static bool initialized = false;
    if (initialized) {
        return true;
    }
    if (NFD_Init() != NFD_OKAY) {
        if (out_error != nullptr) {
            const char *err = NFD_GetError();
            *out_error = err ? err : "nfd init failed";
        }
        return false;
    }
    std::atexit([]() { NFD_Quit(); });
    initialized = true;
    return true;
}

}  // namespace

const char *UnifiedPluginStatusLabel(UnifiedPluginStatus status) {
    switch (status) {
        case UnifiedPluginStatus::Ready:
            return "READY";
        case UnifiedPluginStatus::Loading:
            return "LOADING";
        case UnifiedPluginStatus::Error:
            return "ERROR";
        case UnifiedPluginStatus::Disabled:
            return "DISABLED";
        case UnifiedPluginStatus::NotLoaded:
        default:
            return "NOT_LOADED";
    }
}

ImVec4 UnifiedPluginStatusColor(UnifiedPluginStatus status) {
    switch (status) {
        case UnifiedPluginStatus::Ready:
            return ImVec4(0.35f, 0.9f, 0.45f, 1.0f);
        case UnifiedPluginStatus::Loading:
            return ImVec4(0.45f, 0.82f, 1.0f, 1.0f);
        case UnifiedPluginStatus::Disabled:
            return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        case UnifiedPluginStatus::Error:
            return ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        case UnifiedPluginStatus::NotLoaded:
        default:
            return ImVec4(0.9f, 0.75f, 0.25f, 1.0f);
    }
}

const char *UnifiedPluginKindLabel(UnifiedPluginKind kind) {
    switch (kind) {
        case UnifiedPluginKind::Asr:
            return "ASR";
        case UnifiedPluginKind::SceneClassifier:
            return "Scene";
        case UnifiedPluginKind::Facemesh:
            return "Facemesh";
        case UnifiedPluginKind::Ocr:
            return "OCR";
        case UnifiedPluginKind::BehaviorUser:
        default:
            return "Behavior";
    }
}

std::string JoinAssetsUi(const std::vector<std::string> &assets) {
    if (assets.empty()) {
        return "(empty)";
    }
    std::ostringstream oss;
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (i > 0) {
            oss << "\n";
        }
        oss << assets[i];
    }
    return oss.str();
}

std::vector<std::string> SplitCsvUi(const char *csv) {
    std::vector<std::string> out;
    if (!csv || !csv[0]) {
        return out;
    }
    std::istringstream iss(csv);
    std::string token;
    while (std::getline(iss, token, ',')) {
        std::string trimmed;
        trimmed.reserve(token.size());
        bool leading = true;
        for (char ch : token) {
            if (leading && std::isspace(static_cast<unsigned char>(ch))) {
                continue;
            }
            leading = false;
            trimmed.push_back(ch);
        }
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
            trimmed.pop_back();
        }
        if (!trimmed.empty()) {
            out.push_back(trimmed);
        }
    }
    return out;
}

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
    ImGui::SeparatorText("Recent Error ");
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

    constexpr std::size_t kMaxChars = 4000;
    bool truncated = false;

    int lines = 1;
    std::size_t cut_pos = std::string::npos;
    const std::size_t n = text.size();
    const std::size_t scan_limit = std::min(n, kMaxChars + 1);
    for (std::size_t i = 0; i < scan_limit; ++i) {
        if (text[i] == '\n') {
            lines += 1;
            if (lines > max_lines) {
                cut_pos = i;
                truncated = true;
                break;
            }
        }
        if (i >= kMaxChars) {
            cut_pos = i;
            truncated = true;
            break;
        }
    }

    if (!truncated) {
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
        ImGui::TextUnformatted("");
    } else {
        ImGui::TextWrapped("%s", display.c_str());
        if (truncated) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "... truncated to max %d lines", max_lines);
        }
    }
    ImGui::EndChild();
}

bool RenderFilePickerButton(const char *button_label,
                            std::string *out_path,
                            const char *filter_name,
                            const char *filter_spec,
                            const char *default_path,
                            std::string *out_error) {
    if (!ImGui::Button(button_label)) {
        return false;
    }

    if (out_error != nullptr) {
        out_error->clear();
    }

    if (out_path == nullptr) {
        if (out_error != nullptr) {
            *out_error = "invalid output path";
        }
        return false;
    }

    if (!EnsureNfdInitialized(out_error)) {
        return false;
    }

    const nfdu8filteritem_t filters[] = {
        {filter_name ? filter_name : "All", filter_spec ? filter_spec : "*"},
    };

    nfdu8char_t *picked = nullptr;
    const nfdresult_t res = NFD_OpenDialogU8(&picked,
                                             filters,
                                             1,
                                             default_path && default_path[0] ? default_path : nullptr);

    if (res == NFD_OKAY && picked != nullptr) {
        *out_path = picked;
        NFD_FreePathU8(picked);
    } else if (res == NFD_ERROR) {
        if (out_error != nullptr) {
            const char *err = NFD_GetError();
            *out_error = err ? err : "nfd open dialog failed";
        }
    }

    return res == NFD_OKAY;
}

}  // namespace desktoper2D
