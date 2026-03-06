#include "k2d/lifecycle/ui/app_debug_ui.h"

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
#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

namespace {

const char *TaskPrimaryCategoryNameUi(TaskPrimaryCategory c) {
    switch (c) {
        case TaskPrimaryCategory::Work: return "work";
        case TaskPrimaryCategory::Game: return "game";
        default: return "unknown";
    }
}

const char *TaskSecondaryCategoryNameUi(TaskSecondaryCategory c) {
    switch (c) {
        case TaskSecondaryCategory::WorkCoding: return "coding";
        case TaskSecondaryCategory::WorkDebugging: return "debugging";
        case TaskSecondaryCategory::WorkReadingDocs: return "reading_docs";
        case TaskSecondaryCategory::WorkMeetingNotes: return "meeting_notes";
        case TaskSecondaryCategory::GameLobby: return "lobby";
        case TaskSecondaryCategory::GameMatch: return "match";
        case TaskSecondaryCategory::GameSettlement: return "settlement";
        case TaskSecondaryCategory::GameMenu: return "menu";
        default: return "unknown";
    }
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
    ImGui::SeparatorText("Recent Error (Highlight)");
    if (err.empty()) {
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "None");
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "ERROR");
    ImGui::TextWrapped("%s", err.c_str());
}

enum class HealthState {
    Green,
    Yellow,
    Red,
};

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

RuntimeErrorDomain PickRecentErrorDomain(const AppRuntime &runtime) {
    const std::array<const RuntimeErrorInfo *, 9> infos = {
        &runtime.chat_error_info,
        &runtime.asr_error_info,
        &runtime.plugin_error_info,
        &runtime.perception_state.facemesh_error_info,
        &runtime.perception_state.ocr_error_info,
        &runtime.perception_state.scene_error_info,
        &runtime.perception_state.capture_error_info,
        &runtime.perception_state.system_context_error_info,
        &runtime.reminder_error_info,
    };
    for (const RuntimeErrorInfo *info : infos) {
        if (info->code != RuntimeErrorCode::Ok) {
            return info->domain;
        }
    }
    return RuntimeErrorDomain::None;
}

struct RuntimeErrorRow {
    const char *label = "";
    const RuntimeErrorInfo *info = nullptr;
    int recent_seq = 0;
};

std::vector<RuntimeErrorRow> BuildRuntimeErrorRows(const AppRuntime &runtime) {
    return {
        {"Chat", &runtime.chat_error_info, 0},
        {"ASR", &runtime.asr_error_info, 1},
        {"Plugin.Worker", &runtime.plugin_error_info, 2},
        {"Perception.FaceMesh", &runtime.perception_state.facemesh_error_info, 3},
        {"Perception.OCR", &runtime.perception_state.ocr_error_info, 4},
        {"Perception.Scene", &runtime.perception_state.scene_error_info, 5},
        {"Perception.Capture", &runtime.perception_state.capture_error_info, 6},
        {"Perception.SystemContext", &runtime.perception_state.system_context_error_info, 7},
        {"Reminder", &runtime.reminder_error_info, 8},
    };
}

void RenderRuntimeErrorClassificationTable(const AppRuntime &runtime) {
    std::vector<RuntimeErrorRow> rows = BuildRuntimeErrorRows(runtime);
    std::stable_sort(rows.begin(), rows.end(), [](const RuntimeErrorRow &a, const RuntimeErrorRow &b) {
        if (a.info->count != b.info->count) {
            return a.info->count > b.info->count;
        }
        return a.recent_seq < b.recent_seq;
    });

    std::string all_errors;
    all_errors.reserve(1024);
    for (const auto &row : rows) {
        all_errors += row.label;
        all_errors += " | count=";
        all_errors += std::to_string(static_cast<long long>(row.info->count));
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

    if (ImGui::Button("Copy All Errors")) {
        ImGui::SetClipboardText(all_errors.c_str());
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("recent_seq 越小表示越近期（当前为近似时序）");

    if (ImGui::BeginTable("runtime_error_table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Recent", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthStretch, 0.24f);
        ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch, 0.24f);
        ImGui::TableHeadersRow();

        for (const auto &row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%lld", static_cast<long long>(row.info->count));

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("T-%d", row.recent_seq);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s.%s", RuntimeErrorDomainName(row.info->domain), RuntimeErrorCodeName(row.info->code));

            ImGui::TableSetColumnIndex(4);
            if (row.info->detail.empty()) {
                ImGui::TextUnformatted("(none)");
            } else {
                ImGui::TextWrapped("%s", row.info->detail.c_str());
            }
        }
        ImGui::EndTable();
    }
}

std::string LimitTextLines(const std::string &text, int max_lines, bool *out_truncated = nullptr) {
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

void RenderLongTextBlock(const char *title, const char *child_id, std::string *text, int max_lines, float child_h = 90.0f) {
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

void ResetPerceptionRuntimeState(PerceptionPipelineState &state) {
    state.screen_capture_poll_accum_sec = 0.0f;
    state.screen_capture_last_error.clear();
    state.screen_capture_success_count = 0;
    state.screen_capture_fail_count = 0;
    ClearRuntimeError(state.capture_error_info);

    state.scene_classifier_last_error.clear();
    state.scene_result = SceneClassificationResult{};
    state.scene_total_runs = 0;
    state.scene_total_latency_ms = 0;
    state.scene_avg_latency_ms = 0.0f;
    ClearRuntimeError(state.scene_error_info);

    state.ocr_last_error.clear();
    state.ocr_result = OcrResult{};
    state.ocr_last_stable_result = OcrResult{};
    state.ocr_skipped_due_timeout = false;
    state.ocr_total_runs = 0;
    state.ocr_total_latency_ms = 0;
    state.ocr_avg_latency_ms = 0.0f;
    state.ocr_total_raw_lines = 0;
    state.ocr_total_kept_lines = 0;
    state.ocr_total_dropped_low_conf_lines = 0;
    state.ocr_discard_rate = 0.0f;
    state.ocr_conf_low_count = 0;
    state.ocr_conf_mid_count = 0;
    state.ocr_conf_high_count = 0;
    state.ocr_summary_candidate.clear();
    state.ocr_summary_stable.clear();
    state.ocr_summary_consistent_count = 0;
    ClearRuntimeError(state.ocr_error_info);

    state.system_context_snapshot = SystemContextSnapshot{};
    state.system_context_last_error.clear();
    ClearRuntimeError(state.system_context_error_info);

    state.camera_facemesh_last_error.clear();
    state.face_emotion_result = FaceEmotionResult{};
    state.face_total_runs = 0;
    state.face_total_latency_ms = 0;
    state.face_avg_latency_ms = 0.0f;
    ClearRuntimeError(state.facemesh_error_info);

    state.blackboard = PerceptionBlackboard{};
}

void ResetAllRuntimeErrorCounters(AppRuntime &runtime) {
    auto reset_err = [](RuntimeErrorInfo &err) {
        ClearRuntimeError(err);
        err.count = 0;
    };

    reset_err(runtime.perception_state.capture_error_info);
    reset_err(runtime.perception_state.scene_error_info);
    reset_err(runtime.perception_state.ocr_error_info);
    reset_err(runtime.perception_state.system_context_error_info);
    reset_err(runtime.perception_state.facemesh_error_info);
    reset_err(runtime.plugin_error_info);
    reset_err(runtime.asr_error_info);
    reset_err(runtime.chat_error_info);
    reset_err(runtime.reminder_error_info);
}

JsonValue BuildRuntimeSnapshotJson(const AppRuntime &runtime) {
    JsonObject root;
    root.emplace("schema", JsonValue::makeString("k2d.runtime.snapshot.v1"));
    root.emplace("ts_unix_sec", JsonValue::makeNumber(static_cast<double>(std::time(nullptr))));

    JsonObject perf;
    perf.emplace("fps", JsonValue::makeNumber(runtime.debug_fps));
    perf.emplace("frame_ms", JsonValue::makeNumber(runtime.debug_frame_ms));
    root.emplace("perf", JsonValue::makeObject(std::move(perf)));

    JsonObject perception;
    perception.emplace("capture_ready", JsonValue::makeBool(runtime.perception_state.screen_capture_ready));
    perception.emplace("scene_ready", JsonValue::makeBool(runtime.perception_state.scene_classifier_ready));
    perception.emplace("ocr_ready", JsonValue::makeBool(runtime.perception_state.ocr_ready));
    perception.emplace("facemesh_ready", JsonValue::makeBool(runtime.perception_state.camera_facemesh_ready));
    perception.emplace("capture_success", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.screen_capture_success_count)));
    perception.emplace("capture_fail", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.screen_capture_fail_count)));
    perception.emplace("scene_label", JsonValue::makeString(runtime.perception_state.scene_result.label));
    perception.emplace("ocr_summary", JsonValue::makeString(runtime.perception_state.ocr_result.summary));
    root.emplace("perception", JsonValue::makeObject(std::move(perception)));

    JsonObject err_counts;
    err_counts.emplace("capture", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.capture_error_info.count)));
    err_counts.emplace("scene", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.scene_error_info.count)));
    err_counts.emplace("ocr", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.ocr_error_info.count)));
    err_counts.emplace("facemesh", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.facemesh_error_info.count)));
    err_counts.emplace("system_context", JsonValue::makeNumber(static_cast<double>(runtime.perception_state.system_context_error_info.count)));
    err_counts.emplace("plugin", JsonValue::makeNumber(static_cast<double>(runtime.plugin_error_info.count)));
    err_counts.emplace("asr", JsonValue::makeNumber(static_cast<double>(runtime.asr_error_info.count)));
    err_counts.emplace("chat", JsonValue::makeNumber(static_cast<double>(runtime.chat_error_info.count)));
    err_counts.emplace("reminder", JsonValue::makeNumber(static_cast<double>(runtime.reminder_error_info.count)));
    root.emplace("error_counts", JsonValue::makeObject(std::move(err_counts)));

    return JsonValue::makeObject(std::move(root));
}

bool ExportRuntimeSnapshotJson(const AppRuntime &runtime, const char *path, std::string *out_error) {
    const JsonValue snapshot = BuildRuntimeSnapshotJson(runtime);
    const std::string text = StringifyJson(snapshot, 2);

    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) {
        if (out_error) {
            *out_error = std::string("open snapshot file failed: ") + SDL_GetError();
        }
        return false;
    }

    const size_t n = text.size();
    const size_t w = SDL_WriteIO(io, text.data(), n);
    SDL_CloseIO(io);

    if (w != n) {
        if (out_error) {
            *out_error = "write snapshot file failed";
        }
        return false;
    }

    if (out_error) {
        out_error->clear();
    }
    return true;
}

void TriggerSingleStepSampling(AppRuntime &runtime) {
    runtime.perception_state.screen_capture_poll_accum_sec =
        std::max(0.1f, runtime.perception_state.screen_capture_poll_interval_sec);
    runtime.reminder_poll_accum_sec = 1.0f;
    runtime.asr_poll_accum_sec = 0.02f;
    runtime.runtime_observability_log_accum_sec =
        std::max(0.2f, runtime.runtime_observability_log_interval_sec);
}

std::string DetectParamPrefix(const std::string &param_id) {
    if (param_id.empty()) return "(empty)";
    const std::size_t us = param_id.find('_');
    if (us != std::string::npos && us > 0) {
        return param_id.substr(0, us);
    }
    std::size_t cut = 0;
    while (cut < param_id.size()) {
        const unsigned char ch = static_cast<unsigned char>(param_id[cut]);
        if ((ch >= 'A' && ch <= 'Z' && cut > 0) || (ch >= '0' && ch <= '9')) {
            break;
        }
        ++cut;
    }
    if (cut == 0) return "misc";
    return param_id.substr(0, cut);
}

std::string DetectParamSemanticGroup(const std::string &param_id) {
    std::string id_lower = param_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (id_lower.find("eye") != std::string::npos) return "Eye";
    if (id_lower.find("brow") != std::string::npos) return "Brow";
    if (id_lower.find("mouth") != std::string::npos || id_lower.find("lip") != std::string::npos) return "Mouth";
    if (id_lower.find("head") != std::string::npos || id_lower.find("neck") != std::string::npos) return "Head";
    if (id_lower.find("hair") != std::string::npos || id_lower.find("bang") != std::string::npos) return "Hair";
    if (id_lower.find("body") != std::string::npos || id_lower.find("arm") != std::string::npos) return "Body";
    return "Other";
}

using ParamGroup = std::pair<std::string, std::vector<int>>;

std::vector<ParamGroup> BuildParamGroups(const AppRuntime &runtime, int group_mode) {
    std::map<std::string, std::vector<int>, std::less<>> grouped;
    for (int i = 0; i < static_cast<int>(runtime.model.parameters.size()); ++i) {
        const auto &p = runtime.model.parameters[static_cast<std::size_t>(i)];
        const std::string key = (group_mode == 1) ? DetectParamSemanticGroup(p.id) : DetectParamPrefix(p.id);
        grouped[key].push_back(i);
    }

    std::vector<ParamGroup> out;
    out.reserve(grouped.size());
    for (auto &kv : grouped) {
        out.emplace_back(kv.first, std::move(kv.second));
    }
    return out;
}

const char *BindingTypeNameUi(BindingType t) {
    switch (t) {
        case BindingType::PosX: return "PosX";
        case BindingType::PosY: return "PosY";
        case BindingType::RotDeg: return "RotDeg";
        case BindingType::ScaleX: return "ScaleX";
        case BindingType::ScaleY: return "ScaleY";
        case BindingType::Opacity: return "Opacity";
        default: return "Unknown";
    }
}

void UpsertBinding(ModelPart &part, int param_index, BindingType type, float in_min, float in_max, float out_min, float out_max) {
    for (auto &b : part.bindings) {
        if (b.param_index == param_index && b.type == type) {
            b.in_min = in_min;
            b.in_max = in_max;
            b.out_min = out_min;
            b.out_max = out_max;
            return;
        }
    }

    ParamBinding b{};
    b.param_index = param_index;
    b.type = type;
    b.in_min = in_min;
    b.in_max = in_max;
    b.out_min = out_min;
    b.out_max = out_max;
    part.bindings.push_back(b);
}

void UpsertTimelineKeyframe(AnimationChannel &ch, float time_sec, float value) {
    for (auto &kf : ch.keyframes) {
        if (std::abs(kf.time_sec - time_sec) < 1e-4f) {
            kf.value = value;
            return;
        }
    }
    ch.keyframes.push_back(TimelineKeyframe{.time_sec = time_sec, .value = value});
    std::sort(ch.keyframes.begin(), ch.keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
        return a.time_sec < b.time_sec;
    });
}

void RenderOverviewRuntimeHealth(const AppRuntime &runtime) {
    ImGui::SeparatorText("Subsystem Health Lights");
    if (ImGui::BeginTable("overview_health_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Subsystem", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthStretch, 0.45f);

        const bool perception_ok = runtime.perception_state.screen_capture_ready &&
                                   runtime.perception_state.scene_classifier_ready &&
                                   runtime.perception_state.ocr_ready &&
                                   runtime.perception_state.camera_facemesh_ready;
        RenderHealthLampRow("Perception",
                            perception_ok ? HealthState::Green : HealthState::Yellow,
                            perception_ok ? "all sensors ready" : "partial readiness");

        const bool plugin_degraded = runtime.plugin_auto_disabled || runtime.plugin_timeout_rate > 0.10 ||
                                     !runtime.plugin_last_error.empty();
        RenderHealthLampRow("Plugin Worker",
                            plugin_degraded ? HealthState::Red : HealthState::Green,
                            plugin_degraded ? "degraded or auto-disabled" : "healthy");

        const bool asr_available = runtime.feature_asr_enabled && runtime.asr_ready;
        RenderHealthLampRow("ASR",
                            asr_available ? HealthState::Green : (runtime.feature_asr_enabled ? HealthState::Yellow : HealthState::Red),
                            asr_available ? "available" : (runtime.feature_asr_enabled ? "enabled but not ready" : "feature disabled"));

        const bool chat_available = runtime.feature_chat_enabled && runtime.chat_ready;
        RenderHealthLampRow("Chat",
                            chat_available ? HealthState::Green : (runtime.feature_chat_enabled ? HealthState::Yellow : HealthState::Red),
                            chat_available ? "available" : (runtime.feature_chat_enabled ? "enabled but not ready" : "feature disabled"));
        ImGui::EndTable();
    }
}

}  // namespace

void RenderRuntimeDebugSummary(const AppRuntime &runtime) {
    ImGui::Text("FPS: %.2f", runtime.debug_fps);
    ImGui::Text("Frame: %.2f ms", runtime.debug_frame_ms);
    ImGui::Text("Model Loaded: %s", runtime.model_loaded ? "Yes" : "No");
    ImGui::Text("PartCount: %d", static_cast<int>(runtime.model.parts.size()));
}

void RenderAppDebugUi(AppRuntime &runtime) {
    static std::string runtime_ops_status;
    if (ImGui::CollapsingHeader("总览", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Status Card (Read Only)");
        if (ImGui::BeginTable("overview_status_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);

            char buf[128] = {};
            SDL_snprintf(buf, sizeof(buf), "%.2f ms", runtime.debug_frame_ms);
            RenderOverviewTableRow("Frame Time", buf);

            SDL_snprintf(buf, sizeof(buf), "%.2f", runtime.debug_fps);
            RenderOverviewTableRow("FPS", buf);

            SDL_snprintf(buf, sizeof(buf), "%d count", static_cast<int>(runtime.model.parts.size()));
            RenderOverviewTableRow("Model Parts", buf);

            RenderOverviewTableRow("Model Loaded", runtime.model_loaded ? "Yes" : "No");

            const bool plugin_degraded = runtime.plugin_auto_disabled || runtime.plugin_timeout_rate > 0.10 ||
                                         !runtime.plugin_last_error.empty();
            RenderOverviewTableRow("Plugin Degraded", plugin_degraded ? "Yes" : "No");

            SDL_snprintf(buf, sizeof(buf), "%.2f%%", runtime.plugin_timeout_rate * 100.0);
            RenderOverviewTableRow("Plugin Timeout Rate", buf);

            RenderOverviewTableRow("ASR Availability", (runtime.feature_asr_enabled && runtime.asr_ready) ? "Available" : "Unavailable");
            RenderOverviewTableRow("Chat Availability", (runtime.feature_chat_enabled && runtime.chat_ready) ? "Available" : "Unavailable");

            const RuntimeErrorDomain recent_domain = PickRecentErrorDomain(runtime);
            RenderOverviewTableRow("Recent Error Domain", RuntimeErrorDomainName(recent_domain));

            ImGui::EndTable();
        }
        RenderOverviewRuntimeHealth(runtime);

        ImGui::SeparatorText("Param Card (Editable)");
        ImGui::Checkbox("Show Debug Stats", &runtime.show_debug_stats);
        if (ImGui::Checkbox("Manual Param Mode", &runtime.manual_param_mode)) {
            if (runtime.model_loaded) {
                runtime.model.animation_channels_enabled = !runtime.manual_param_mode;
                if (runtime.manual_param_mode) {
                    for (ModelParameter &p : runtime.model.parameters) {
                        p.param.SetTarget(p.param.value());
                    }
                }
            }
        }
        ImGui::Checkbox("GUI Enabled", &runtime.gui_enabled);
        ImGui::Checkbox("Hair Spring", &runtime.model.enable_hair_spring);
        ImGui::Checkbox("Simple Mask", &runtime.model.enable_simple_mask);

        ImGui::SeparatorText("Head Pat Interaction");
        ImGui::Text("Head Hovering: %s", runtime.interaction_state.head_pat_hovering ? "Yes" : "No");
        ImGui::Text("React TTL: %.3f s", runtime.interaction_state.head_pat_react_ttl);
        const float pat_ratio = std::clamp(runtime.interaction_state.head_pat_react_ttl / 0.35f, 0.0f, 1.0f);
        ImGui::ProgressBar(pat_ratio, ImVec2(-1.0f, 0.0f), "Pat React");

        ImGui::SeparatorText("Feature Toggles");
        ImGui::Checkbox("Enable Scene Classifier", &runtime.feature_scene_classifier_enabled);
        ImGui::Checkbox("Enable OCR", &runtime.feature_ocr_enabled);
        ImGui::Checkbox("Enable Face Emotion", &runtime.feature_face_emotion_enabled);
        ImGui::Checkbox("Enable ASR", &runtime.feature_asr_enabled);

        std::string overview_error;
        RenderModuleLatestErrorCard(overview_error);

        ImGui::SeparatorText("Param Panel Enhanced (Group/Batch Bind)");
        if (runtime.model_loaded && !runtime.model.parameters.empty()) {
            const char *group_modes[] = {"Prefix", "Semantic"};
            ImGui::Combo("Param Group Mode", &runtime.param_group_mode, group_modes, 2);

            std::vector<ParamGroup> groups = BuildParamGroups(runtime, runtime.param_group_mode);
            if (!groups.empty()) {
                runtime.selected_param_group_index = std::clamp(runtime.selected_param_group_index, 0, static_cast<int>(groups.size()) - 1);

                if (ImGui::BeginCombo("Param Group", groups[static_cast<std::size_t>(runtime.selected_param_group_index)].first.c_str())) {
                    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
                        const bool selected = i == runtime.selected_param_group_index;
                        std::string label = groups[static_cast<std::size_t>(i)].first + " (" +
                                            std::to_string(groups[static_cast<std::size_t>(i)].second.size()) + ")";
                        if (ImGui::Selectable(label.c_str(), selected)) {
                            runtime.selected_param_group_index = i;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const auto &selected_group = groups[static_cast<std::size_t>(runtime.selected_param_group_index)];
                ImGui::Text("Group Param Count: %d", static_cast<int>(selected_group.second.size()));
                if (!selected_group.second.empty()) {
                    std::string preview;
                    const int max_preview = std::min(5, static_cast<int>(selected_group.second.size()));
                    for (int i = 0; i < max_preview; ++i) {
                        const int idx = selected_group.second[static_cast<std::size_t>(i)];
                        if (idx >= 0 && idx < static_cast<int>(runtime.model.parameters.size())) {
                            if (!preview.empty()) preview += ", ";
                            preview += runtime.model.parameters[static_cast<std::size_t>(idx)].id;
                        }
                    }
                    ImGui::TextWrapped("Preview: %s%s", preview.c_str(), selected_group.second.size() > static_cast<std::size_t>(max_preview) ? " ..." : "");
                }

                const char *binding_props[] = {"PosX", "PosY", "RotDeg", "ScaleX", "ScaleY", "Opacity"};
                ImGui::Combo("Bind Property", &runtime.batch_bind_prop_type, binding_props, 6);
                runtime.batch_bind_prop_type = std::clamp(runtime.batch_bind_prop_type, 0, 5);

                ImGui::SliderFloat("Bind In Min", &runtime.batch_bind_in_min, -2.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Bind In Max", &runtime.batch_bind_in_max, -2.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Bind Out Min", &runtime.batch_bind_out_min, -180.0f, 180.0f, "%.2f");
                ImGui::SliderFloat("Bind Out Max", &runtime.batch_bind_out_max, -180.0f, 180.0f, "%.2f");

                const BindingType bt = static_cast<BindingType>(runtime.batch_bind_prop_type);
                if (ImGui::Button("Apply Batch Bind -> Selected Part")) {
                    if (runtime.selected_part_index >= 0 &&
                        runtime.selected_part_index < static_cast<int>(runtime.model.parts.size())) {
                        auto &part = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)];
                        for (int param_idx : selected_group.second) {
                            UpsertBinding(part,
                                          param_idx,
                                          bt,
                                          runtime.batch_bind_in_min,
                                          runtime.batch_bind_in_max,
                                          runtime.batch_bind_out_min,
                                          runtime.batch_bind_out_max);
                        }
                        runtime.editor_status = "batch bind applied to selected part: " + part.id +
                                                " | group=" + selected_group.first +
                                                " | prop=" + BindingTypeNameUi(bt);
                        runtime.editor_status_ttl = 2.5f;
                    } else {
                        runtime.editor_status = "batch bind failed: no selected part";
                        runtime.editor_status_ttl = 2.5f;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply Batch Bind -> All Parts")) {
                    int touched = 0;
                    for (auto &part : runtime.model.parts) {
                        for (int param_idx : selected_group.second) {
                            UpsertBinding(part,
                                          param_idx,
                                          bt,
                                          runtime.batch_bind_in_min,
                                          runtime.batch_bind_in_max,
                                          runtime.batch_bind_out_min,
                                          runtime.batch_bind_out_max);
                        }
                        touched += 1;
                    }
                    runtime.editor_status = "batch bind applied to all parts=" + std::to_string(touched) +
                                            " | group=" + selected_group.first +
                                            " | prop=" + BindingTypeNameUi(bt);
                    runtime.editor_status_ttl = 2.5f;
                }
            }
        } else {
            ImGui::TextDisabled("model/parameters unavailable");
        }
    }

    if (ImGui::CollapsingHeader("Timeline v1", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable Timeline", &runtime.timeline_enabled);
        runtime.model.animation_channels_enabled = runtime.timeline_enabled;

        ImGui::SliderFloat("Timeline Cursor (s)", &runtime.timeline_cursor_sec, 0.0f, std::max(0.1f, runtime.timeline_duration_sec), "%.2f");
        ImGui::SliderFloat("Timeline Duration (s)", &runtime.timeline_duration_sec, 0.5f, 30.0f, "%.1f");

        if (runtime.model.parameters.empty()) {
            ImGui::TextDisabled("no parameters");
        } else {
            if (ImGui::Button("Add Channel")) {
                const int pidx = runtime.selected_param_index >= 0 &&
                                 runtime.selected_param_index < static_cast<int>(runtime.model.parameters.size())
                                 ? runtime.selected_param_index
                                 : 0;
                AnimationChannel ch{};
                ch.id = "timeline_" + runtime.model.parameters[static_cast<std::size_t>(pidx)].id;
                ch.param_index = pidx;
                ch.enabled = true;
                ch.weight = 1.0f;
                ch.blend = AnimationBlendMode::Override;
                ch.timeline_interp = TimelineInterpolation::Linear;
                runtime.model.animation_channels.push_back(std::move(ch));
                runtime.timeline_selected_channel_index = static_cast<int>(runtime.model.animation_channels.size()) - 1;
            }

            if (!runtime.model.animation_channels.empty()) {
                runtime.timeline_selected_channel_index = std::clamp(runtime.timeline_selected_channel_index,
                                                                     0,
                                                                     static_cast<int>(runtime.model.animation_channels.size()) - 1);
                auto &ch = runtime.model.animation_channels[static_cast<std::size_t>(runtime.timeline_selected_channel_index)];

                if (ImGui::BeginCombo("Channel", ch.id.c_str())) {
                    for (int i = 0; i < static_cast<int>(runtime.model.animation_channels.size()); ++i) {
                        const auto &ci = runtime.model.animation_channels[static_cast<std::size_t>(i)];
                        const bool sel = i == runtime.timeline_selected_channel_index;
                        if (ImGui::Selectable(ci.id.c_str(), sel)) {
                            runtime.timeline_selected_channel_index = i;
                        }
                    }
                    ImGui::EndCombo();
                }

                ImGui::Checkbox("Channel Enabled", &ch.enabled);
                const char *interp_items[] = {"Step", "Linear", "Hermite"};
                int interp_idx = ch.timeline_interp == TimelineInterpolation::Step ? 0 :
                                 (ch.timeline_interp == TimelineInterpolation::Linear ? 1 : 2);
                if (ImGui::Combo("Interpolation", &interp_idx, interp_items, 3)) {
                    ch.timeline_interp = interp_idx == 0 ? TimelineInterpolation::Step :
                                       (interp_idx == 1 ? TimelineInterpolation::Linear : TimelineInterpolation::Hermite);
                }

                const char *wrap_items[] = {"Clamp", "Loop", "PingPong"};
                int wrap_idx = ch.timeline_wrap == TimelineWrapMode::Clamp ? 0 :
                               (ch.timeline_wrap == TimelineWrapMode::Loop ? 1 : 2);
                if (ImGui::Combo("Wrap", &wrap_idx, wrap_items, 3)) {
                    ch.timeline_wrap = wrap_idx == 0 ? TimelineWrapMode::Clamp :
                                     (wrap_idx == 1 ? TimelineWrapMode::Loop : TimelineWrapMode::PingPong);
                }

                int param_idx = std::clamp(ch.param_index, 0, static_cast<int>(runtime.model.parameters.size()) - 1);
                if (ImGui::BeginCombo("Target Param", runtime.model.parameters[static_cast<std::size_t>(param_idx)].id.c_str())) {
                    for (int i = 0; i < static_cast<int>(runtime.model.parameters.size()); ++i) {
                        const bool sel = i == param_idx;
                        const auto &pid = runtime.model.parameters[static_cast<std::size_t>(i)].id;
                        if (ImGui::Selectable(pid.c_str(), sel)) {
                            param_idx = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                ch.param_index = param_idx;

                if (ImGui::Button("Add/Update Keyframe At Cursor")) {
                    const auto &p = runtime.model.parameters[static_cast<std::size_t>(param_idx)].param;
                    const float v = p.target();
                    UpsertTimelineKeyframe(ch, runtime.timeline_cursor_sec, v);
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Last Keyframe") && !ch.keyframes.empty()) {
                    ch.keyframes.pop_back();
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Channel")) {
                    runtime.model.animation_channels.erase(runtime.model.animation_channels.begin() + runtime.timeline_selected_channel_index);
                    runtime.timeline_selected_channel_index = std::max(0, runtime.timeline_selected_channel_index - 1);
                }

                ImGui::Text("Keyframes: %d", static_cast<int>(ch.keyframes.size()));

                ImGui::SeparatorText("Track Editor (Drag)");
                const float track_h = 180.0f;
                const ImVec2 track_size(ImGui::GetContentRegionAvail().x, track_h);
                const ImVec2 track_pos = ImGui::GetCursorScreenPos();
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImGui::InvisibleButton("##timeline_track_drag", track_size,
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

                const float duration = std::max(0.001f, runtime.timeline_duration_sec);
                float vmin = -1.0f;
                float vmax = 1.0f;
                if (param_idx >= 0 && param_idx < static_cast<int>(runtime.model.parameters.size())) {
                    const auto &spec = runtime.model.parameters[static_cast<std::size_t>(param_idx)].param.spec();
                    vmin = std::min(spec.min_value, spec.max_value);
                    vmax = std::max(spec.min_value, spec.max_value);
                    if (std::abs(vmax - vmin) < 1e-6f) {
                        vmax = vmin + 1.0f;
                    }
                }

                auto time_to_x = [&](float t) {
                    const float nt = std::clamp(t / duration, 0.0f, 1.0f);
                    return track_pos.x + nt * track_size.x;
                };
                auto value_to_y = [&](float v) {
                    const float nv = std::clamp((v - vmin) / (vmax - vmin), 0.0f, 1.0f);
                    return track_pos.y + (1.0f - nv) * track_size.y;
                };
                auto x_to_time = [&](float x) {
                    const float nx = std::clamp((x - track_pos.x) / std::max(1.0f, track_size.x), 0.0f, 1.0f);
                    return nx * duration;
                };
                auto y_to_value = [&](float y) {
                    const float ny = std::clamp((y - track_pos.y) / std::max(1.0f, track_size.y), 0.0f, 1.0f);
                    return vmin + (1.0f - ny) * (vmax - vmin);
                };

                dl->AddRectFilled(track_pos,
                                  ImVec2(track_pos.x + track_size.x, track_pos.y + track_size.y),
                                  IM_COL32(26, 28, 32, 220),
                                  6.0f);
                dl->AddRect(track_pos,
                            ImVec2(track_pos.x + track_size.x, track_pos.y + track_size.y),
                            IM_COL32(110, 120, 138, 255),
                            6.0f,
                            0,
                            1.5f);

                for (int g = 1; g < 4; ++g) {
                    const float gx = track_pos.x + track_size.x * (static_cast<float>(g) / 4.0f);
                    dl->AddLine(ImVec2(gx, track_pos.y),
                                ImVec2(gx, track_pos.y + track_size.y),
                                IM_COL32(62, 68, 78, 180),
                                1.0f);
                }
                for (int g = 1; g < 4; ++g) {
                    const float gy = track_pos.y + track_size.y * (static_cast<float>(g) / 4.0f);
                    dl->AddLine(ImVec2(track_pos.x, gy),
                                ImVec2(track_pos.x + track_size.x, gy),
                                IM_COL32(62, 68, 78, 180),
                                1.0f);
                }

                const float cursor_x = time_to_x(runtime.timeline_cursor_sec);
                dl->AddLine(ImVec2(cursor_x, track_pos.y),
                            ImVec2(cursor_x, track_pos.y + track_size.y),
                            IM_COL32(255, 204, 96, 220),
                            1.5f);

                static int dragging_kf_idx = -1;
                static int dragging_channel_idx = -1;
                const bool hovered = ImGui::IsItemHovered();
                const ImVec2 mouse = ImGui::GetIO().MousePos;

                for (std::size_t i = 1; i < ch.keyframes.size(); ++i) {
                    const auto &a = ch.keyframes[i - 1];
                    const auto &b = ch.keyframes[i];
                    dl->AddLine(ImVec2(time_to_x(a.time_sec), value_to_y(a.value)),
                                ImVec2(time_to_x(b.time_sec), value_to_y(b.value)),
                                IM_COL32(120, 200, 255, 230),
                                1.8f);
                }

                int hovered_kf_idx = -1;
                for (std::size_t i = 0; i < ch.keyframes.size(); ++i) {
                    const auto &kf = ch.keyframes[i];
                    const ImVec2 p(time_to_x(kf.time_sec), value_to_y(kf.value));
                    const float dx = mouse.x - p.x;
                    const float dy = mouse.y - p.y;
                    if (dx * dx + dy * dy <= 64.0f) {
                        hovered_kf_idx = static_cast<int>(i);
                    }
                }

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_kf_idx >= 0) {
                    dragging_kf_idx = hovered_kf_idx;
                    dragging_channel_idx = runtime.timeline_selected_channel_index;
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    dragging_kf_idx = -1;
                    dragging_channel_idx = -1;
                }

                if (dragging_channel_idx == runtime.timeline_selected_channel_index &&
                    dragging_kf_idx >= 0 && dragging_kf_idx < static_cast<int>(ch.keyframes.size()) &&
                    ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    auto &kf = ch.keyframes[static_cast<std::size_t>(dragging_kf_idx)];
                    float t_new = x_to_time(mouse.x);
                    float v_new = y_to_value(mouse.y);

                    const float eps = 0.0001f;
                    if (dragging_kf_idx > 0) {
                        t_new = std::max(t_new, ch.keyframes[static_cast<std::size_t>(dragging_kf_idx - 1)].time_sec + eps);
                    }
                    if (dragging_kf_idx + 1 < static_cast<int>(ch.keyframes.size())) {
                        t_new = std::min(t_new, ch.keyframes[static_cast<std::size_t>(dragging_kf_idx + 1)].time_sec - eps);
                    }
                    kf.time_sec = std::clamp(t_new, 0.0f, duration);
                    kf.value = std::clamp(v_new, vmin, vmax);
                }

                for (std::size_t i = 0; i < ch.keyframes.size(); ++i) {
                    const auto &kf = ch.keyframes[i];
                    const ImVec2 p(time_to_x(kf.time_sec), value_to_y(kf.value));
                    const bool active = dragging_channel_idx == runtime.timeline_selected_channel_index &&
                                        dragging_kf_idx == static_cast<int>(i);
                    dl->AddCircleFilled(p, active ? 5.5f : 4.5f, active ? IM_COL32(255, 190, 70, 255) : IM_COL32(210, 230, 255, 255));
                    dl->AddCircle(p, active ? 5.5f : 4.5f, IM_COL32(20, 24, 30, 255), 0, 1.0f);
                }

                if (hovered && hovered_kf_idx >= 0) {
                    const auto &hkf = ch.keyframes[static_cast<std::size_t>(hovered_kf_idx)];
                    ImGui::SetTooltip("KF[%d]\nt=%.3f\nv=%.3f", hovered_kf_idx, hkf.time_sec, hkf.value);
                }

                for (std::size_t i = 0; i < ch.keyframes.size(); ++i) {
                    auto &kf = ch.keyframes[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("t=%.2f v=%.3f", kf.time_sec, kf.value);
                    if (ch.timeline_interp == TimelineInterpolation::Hermite) {
                        ImGui::SliderFloat("inTan", &kf.in_tangent, -20.0f, 20.0f, "%.3f");
                        ImGui::SliderFloat("outTan", &kf.out_tangent, -20.0f, 20.0f, "%.3f");
                        ImGui::SliderFloat("inWeight", &kf.in_weight, 0.0f, 1.0f, "%.3f");
                        ImGui::SliderFloat("outWeight", &kf.out_weight, 0.0f, 1.0f, "%.3f");
                        kf.in_weight = std::clamp(kf.in_weight, 0.0f, 1.0f);
                        kf.out_weight = std::clamp(kf.out_weight, 0.0f, 1.0f);
                    }
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("no channels");
            }
        }
    }

    if (ImGui::CollapsingHeader("感知", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SeparatorText("Status Card (Read Only)");
    ImGui::Text("Capture: %s", runtime.perception_state.screen_capture_ready ? "ready" : "not ready");
    if (!runtime.perception_state.screen_capture_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Capture Error: %s", runtime.perception_state.screen_capture_last_error.c_str());
    }
    ImGui::Text("Scene Classifier: %s", runtime.perception_state.scene_classifier_ready ? "ready" : "not ready");
    if (runtime.perception_state.scene_classifier_ready && !runtime.perception_state.scene_result.label.empty()) {
        ImGui::Text("Scene: %s (%.3f)", runtime.perception_state.scene_result.label.c_str(), runtime.perception_state.scene_result.score);
    }
    if (!runtime.perception_state.scene_classifier_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Scene Error: %s", runtime.perception_state.scene_classifier_last_error.c_str());
    }

    ImGui::Text("OCR: %s (%s)", runtime.perception_state.ocr_ready ? "ready" : "not ready", runtime.feature_ocr_enabled ? "enabled" : "disabled");
    ImGui::Text("Camera FaceMesh: %s (%s)", runtime.perception_state.camera_facemesh_ready ? "ready" : "not ready", runtime.feature_face_emotion_enabled ? "enabled" : "disabled");
    if (runtime.perception_state.camera_facemesh_ready) {
        ImGui::Text("Face: %s", runtime.perception_state.face_emotion_result.face_detected ? "present" : "none");
        ImGui::Text("Emotion: %s (%.2f)",
                    runtime.perception_state.face_emotion_result.emotion_label.empty()
                        ? "(none)"
                        : runtime.perception_state.face_emotion_result.emotion_label.c_str(),
                    runtime.perception_state.face_emotion_result.emotion_score);
        ImGui::Text("HeadPose Y/P/R (deg): %.2f / %.2f / %.2f",
                    runtime.perception_state.face_emotion_result.head_yaw_deg,
                    runtime.perception_state.face_emotion_result.head_pitch_deg,
                    runtime.perception_state.face_emotion_result.head_roll_deg);
        ImGui::Text("Eye Open L/R/Avg: %.2f / %.2f / %.2f",
                    runtime.perception_state.face_emotion_result.eye_open_left,
                    runtime.perception_state.face_emotion_result.eye_open_right,
                    runtime.perception_state.face_emotion_result.eye_open_avg);
        ImGui::Text("Keypoints: %d", static_cast<int>(runtime.perception_state.face_emotion_result.keypoints.size()));
        if (!runtime.perception_state.face_emotion_result.keypoints.empty()) {
            const auto &kp = runtime.perception_state.face_emotion_result.keypoints.front();
            ImGui::Text("KP[0]: %s (%.1f, %.1f) s=%.2f", kp.name.c_str(), kp.x, kp.y, kp.score);
        }
    }
    if (!runtime.perception_state.camera_facemesh_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Camera Error: %s", runtime.perception_state.camera_facemesh_last_error.c_str());
    }

    ImGui::SeparatorText("System Context");
    ImGui::Text("Process: %s", runtime.perception_state.system_context_snapshot.process_name.empty() ? "(empty)" : runtime.perception_state.system_context_snapshot.process_name.c_str());
    ImGui::TextWrapped("Title: %s", runtime.perception_state.system_context_snapshot.window_title.empty() ? "(empty)" : runtime.perception_state.system_context_snapshot.window_title.c_str());
    ImGui::TextWrapped("URL: %s", runtime.perception_state.system_context_snapshot.url_hint.empty() ? "(empty)" : runtime.perception_state.system_context_snapshot.url_hint.c_str());
    if (!runtime.perception_state.system_context_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "System Context Error: %s", runtime.perception_state.system_context_last_error.c_str());
    }

    ImGui::SeparatorText("Param Card (Editable)");
    ImGui::SliderInt("OCR Timeout (ms)", &runtime.perception_state.ocr_timeout_ms, 500, 10000);
    runtime.perception_state.ocr_timeout_ms = std::clamp(runtime.perception_state.ocr_timeout_ms, 500, 10000);
    ImGui::SliderInt("OCR Det Input", &runtime.perception_state.ocr_det_input_size, 160, 1280);
    runtime.perception_state.ocr_det_input_size = std::clamp(runtime.perception_state.ocr_det_input_size, 160, 1280);

    ImGui::SeparatorText("Perception Performance");
    ImGui::SliderFloat("Capture Poll Interval (s)", &runtime.perception_state.screen_capture_poll_interval_sec, 0.1f, 5.0f, "%.2f");
    runtime.perception_state.screen_capture_poll_interval_sec =
        std::clamp(runtime.perception_state.screen_capture_poll_interval_sec, 0.1f, 5.0f);
    const double capture_total = static_cast<double>(runtime.perception_state.screen_capture_success_count +
                                                      runtime.perception_state.screen_capture_fail_count);
    const double capture_success_rate = capture_total > 0.0
                                            ? static_cast<double>(runtime.perception_state.screen_capture_success_count) / capture_total
                                            : 0.0;
    ImGui::Text("Capture Success/Fail: %lld / %lld (%.1f%%)",
                static_cast<long long>(runtime.perception_state.screen_capture_success_count),
                static_cast<long long>(runtime.perception_state.screen_capture_fail_count),
                capture_success_rate * 100.0);
    ImGui::Text("Scene Avg Latency: %.1f ms (runs=%lld)",
                runtime.perception_state.scene_avg_latency_ms,
                static_cast<long long>(runtime.perception_state.scene_total_runs));
    ImGui::Text("Face Avg Latency: %.1f ms (runs=%lld)",
                runtime.perception_state.face_avg_latency_ms,
                static_cast<long long>(runtime.perception_state.face_total_runs));

    if (runtime.perception_state.ocr_ready) {
        RenderLongTextBlock("OCR Summary", "ocr_summary_child", &runtime.perception_state.ocr_result.summary, 8, 100.0f);
        if (!runtime.perception_state.ocr_result.lines.empty()) {
            ImGui::Text("OCR Top1: %s (%.3f)",
                        runtime.perception_state.ocr_result.lines.front().text.c_str(),
                        runtime.perception_state.ocr_result.lines.front().score);
        }
    }

    ImGui::SeparatorText("OCR Quality Metrics");
    ImGui::Text("Avg Latency: %.1f ms", runtime.perception_state.ocr_avg_latency_ms);
    ImGui::Text("Discard Rate: %.2f%%", runtime.perception_state.ocr_discard_rate * 100.0f);
    ImGui::Text("Kept / Raw Lines: %lld / %lld",
                static_cast<long long>(runtime.perception_state.ocr_total_kept_lines),
                static_cast<long long>(runtime.perception_state.ocr_total_raw_lines));
    ImGui::Text("Dropped Low-Conf Lines: %lld (threshold=%.2f)",
                static_cast<long long>(runtime.perception_state.ocr_total_dropped_low_conf_lines),
                runtime.perception_state.ocr_low_conf_threshold);

    const auto conf_total = runtime.perception_state.ocr_conf_low_count +
                            runtime.perception_state.ocr_conf_mid_count +
                            runtime.perception_state.ocr_conf_high_count;
    if (conf_total > 0) {
        const float low_pct = static_cast<float>(runtime.perception_state.ocr_conf_low_count) * 100.0f /
                              static_cast<float>(conf_total);
        const float mid_pct = static_cast<float>(runtime.perception_state.ocr_conf_mid_count) * 100.0f /
                              static_cast<float>(conf_total);
        const float high_pct = static_cast<float>(runtime.perception_state.ocr_conf_high_count) * 100.0f /
                               static_cast<float>(conf_total);
        ImGui::Text("Confidence Dist [<0.5 / 0.5~0.8 / >=0.8]: %.1f%% / %.1f%% / %.1f%%",
                    low_pct,
                    mid_pct,
                    high_pct);
    } else {
        ImGui::Text("Confidence Dist: (no samples)");
    }

    if (!runtime.perception_state.ocr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "OCR Error: %s", runtime.perception_state.ocr_last_error.c_str());
    }

    const std::string &perception_recent_error =
        !runtime.perception_state.camera_facemesh_last_error.empty() ? runtime.perception_state.camera_facemesh_last_error :
        !runtime.perception_state.ocr_last_error.empty() ? runtime.perception_state.ocr_last_error :
        !runtime.perception_state.scene_classifier_last_error.empty() ? runtime.perception_state.scene_classifier_last_error :
        !runtime.perception_state.system_context_last_error.empty() ? runtime.perception_state.system_context_last_error :
        runtime.perception_state.screen_capture_last_error;
    RenderModuleLatestErrorCard(perception_recent_error);

    }

    if (ImGui::CollapsingHeader("映射", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Status Card (Read Only)");
    ImGui::Text("Gate: %s", runtime.face_map_gate_reason.empty() ? "(none)" : runtime.face_map_gate_reason.c_str());
    ImGui::Text("Fallback Active: %s", runtime.face_map_sensor_fallback_active ? "true" : "false");
    ImGui::Text("Fallback Reason: %s",
                runtime.face_map_sensor_fallback_reason.empty() ? "(none)" : runtime.face_map_sensor_fallback_reason.c_str());
    ImGui::Text("Raw Yaw/Pitch/Eye: %.2f / %.2f / %.2f",
                runtime.face_map_raw_yaw_deg,
                runtime.face_map_raw_pitch_deg,
                runtime.face_map_raw_eye_open);
    ImGui::Text("Mapped HeadYaw/HeadPitch/EyeOpen: %.2f / %.2f / %.2f",
                runtime.face_map_out_head_yaw,
                runtime.face_map_out_head_pitch,
                runtime.face_map_out_eye_open);

        ImGui::SeparatorText("Param Card (Editable)");
    ImGui::Checkbox("Enable Face->Param Mapping", &runtime.feature_face_param_mapping_enabled);
    ImGui::SliderFloat("Map Min Confidence", &runtime.face_map_min_confidence, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Map Pose Deadzone (deg)", &runtime.face_map_head_pose_deadzone_deg, 0.0f, 15.0f, "%.1f");
    ImGui::SliderFloat("Map Yaw Max (deg)", &runtime.face_map_yaw_max_deg, 5.0f, 45.0f, "%.1f");
    ImGui::SliderFloat("Map Pitch Max (deg)", &runtime.face_map_pitch_max_deg, 5.0f, 35.0f, "%.1f");
    ImGui::SliderFloat("Map EyeOpen Threshold", &runtime.face_map_eye_open_threshold, 0.0f, 0.9f, "%.2f");
    ImGui::SliderFloat("Map Param Weight", &runtime.face_map_param_weight, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Map Smooth Alpha", &runtime.face_map_smooth_alpha, 0.0f, 1.0f, "%.2f");

    ImGui::SeparatorText("Sensor Fallback Template");
    ImGui::Checkbox("Enable Sensor Fallback", &runtime.face_map_sensor_fallback_enabled);
    ImGui::SliderFloat("Fallback HeadYaw (norm)", &runtime.face_map_sensor_fallback_head_yaw, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Fallback HeadPitch (norm)", &runtime.face_map_sensor_fallback_head_pitch, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Fallback EyeOpen (norm)", &runtime.face_map_sensor_fallback_eye_open, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Fallback Blend Weight", &runtime.face_map_sensor_fallback_weight, 0.0f, 1.0f, "%.2f");

    RenderModuleLatestErrorCard(runtime.face_map_sensor_fallback_reason);

    }

    if (ImGui::CollapsingHeader("ASR/Chat", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Status Card (Read Only)");
    ImGui::Text("ASR: %s (%s)", runtime.asr_ready ? "ready" : "not ready", runtime.feature_asr_enabled ? "enabled" : "disabled");
    RenderLongTextBlock("ASR Text", "asr_text_child", &runtime.asr_last_result.text, 10, 100.0f);
    ImGui::Text("Switch Reason: %s", runtime.asr_last_switch_reason.empty() ? "(none)" : runtime.asr_last_switch_reason.c_str());
    ImGui::Text("RTF: %.3f", runtime.asr_rtf);
    ImGui::Text("WER Proxy: %.3f", runtime.asr_wer_proxy);
    ImGui::Text("Timeout Rate: %.2f%%", runtime.asr_timeout_rate * 100.0);
    ImGui::Text("Cloud Call Ratio: %.2f%%", runtime.asr_cloud_call_ratio * 100.0);
    ImGui::Text("Cloud Success Ratio: %.2f%%", runtime.asr_cloud_success_ratio * 100.0);
    if (!runtime.asr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "ASR Error: %s", runtime.asr_last_error.c_str());
    }

    ImGui::SeparatorText("Plugin Worker");
    ImGui::Text("update_hz: %d", runtime.plugin_current_update_hz);
    ImGui::Text("total updates: %llu", static_cast<unsigned long long>(runtime.plugin_total_update_count));
    ImGui::Text("timeout/exception/internal: %llu / %llu / %llu",
                static_cast<unsigned long long>(runtime.plugin_timeout_count),
                static_cast<unsigned long long>(runtime.plugin_exception_count),
                static_cast<unsigned long long>(runtime.plugin_internal_error_count));
    ImGui::Text("timeout rate: %.2f%%", runtime.plugin_timeout_rate * 100.0);
    ImGui::Text("disable/recover: %llu / %llu",
                static_cast<unsigned long long>(runtime.plugin_disable_count),
                static_cast<unsigned long long>(runtime.plugin_recover_count));
    ImGui::Text("auto_disabled: %s", runtime.plugin_auto_disabled ? "true" : "false");
    if (!runtime.plugin_last_error.empty()) {
        ImGui::TextWrapped("Plugin Last Error: %s", runtime.plugin_last_error.c_str());
    }

    ImGui::SeparatorText("Param Card (Editable)");
    ImGui::Checkbox("Enable Periodic Observability Log", &runtime.runtime_observability_log_enabled);
    ImGui::SliderFloat("Log Interval (s)", &runtime.runtime_observability_log_interval_sec, 0.2f, 10.0f, "%.1f");
    runtime.runtime_observability_log_interval_sec =
        std::clamp(runtime.runtime_observability_log_interval_sec, 0.2f, 10.0f);

    ImGui::SeparatorText("Chat");
    ImGui::Checkbox("Enable Chat", &runtime.feature_chat_enabled);
    ImGui::Checkbox("Prefer Cloud Chat", &runtime.prefer_cloud_chat);
    ImGui::Text("Chat: %s", runtime.chat_ready ? "ready" : "not ready");
    ImGui::InputTextMultiline("##chat_input", runtime.chat_input, sizeof(runtime.chat_input), ImVec2(-1.0f, 72.0f));
    if (ImGui::Button("Send Chat")) {
        if (!(runtime.feature_chat_enabled && runtime.chat_ready && runtime.chat_provider)) {
            runtime.chat_last_error = "chat provider unavailable";
            UpdateRuntimeError(runtime.chat_error_info,
                               RuntimeErrorDomain::Chat,
                               RuntimeErrorCode::InternalError,
                               runtime.chat_last_error);
        } else {
            ChatRequest req{};
            req.user_text = runtime.chat_input;
            req.language = "zh";
            req.max_tokens = 256;
            req.temperature = 0.7f;

            ChatResponse rsp{};
            std::string err;
            const bool ok = runtime.chat_provider->Generate(req, rsp, &err);
            if (ok) {
                runtime.chat_last_answer = std::move(rsp.text);
                runtime.chat_last_switch_reason = rsp.switch_reason;
                runtime.chat_last_error.clear();
                ClearRuntimeError(runtime.chat_error_info);
            } else {
                runtime.chat_last_error = err;
                UpdateRuntimeError(runtime.chat_error_info,
                                   RuntimeErrorDomain::Chat,
                                   RuntimeErrorCode::InferenceFailed,
                                   runtime.chat_last_error);
            }
        }
    }
    RenderLongTextBlock("Answer", "chat_answer_child", &runtime.chat_last_answer, 12, 120.0f);
    ImGui::Text("Switch Reason: %s", runtime.chat_last_switch_reason.empty() ? "(none)" : runtime.chat_last_switch_reason.c_str());
    if (!runtime.chat_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Chat Error: %s", runtime.chat_last_error.c_str());
    }

    const std::string &asr_chat_recent_error =
        !runtime.chat_last_error.empty() ? runtime.chat_last_error :
        !runtime.asr_last_error.empty() ? runtime.asr_last_error :
        runtime.plugin_last_error;
    RenderModuleLatestErrorCard(asr_chat_recent_error);

    }

    if (ImGui::CollapsingHeader("错误分类", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SeparatorText("Runtime Error Classification");
        RenderRuntimeErrorClassificationTable(runtime);
    }

    if (ImGui::CollapsingHeader("Runtime Ops", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Reset Perception State")) {
            ResetPerceptionRuntimeState(runtime.perception_state);
            runtime_ops_status = "Perception state reset";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Error Counters")) {
            ResetAllRuntimeErrorCounters(runtime);
            runtime_ops_status = "Runtime error counters reset";
        }

        if (ImGui::Button("Export Runtime Snapshot (json)")) {
            std::string err;
            if (ExportRuntimeSnapshotJson(runtime, "assets/runtime_snapshot.json", &err)) {
                runtime_ops_status = "Snapshot exported: assets/runtime_snapshot.json";
            } else {
                runtime_ops_status = "Snapshot export failed: " + err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Single-step Sampling")) {
            TriggerSingleStepSampling(runtime);
            runtime_ops_status = "Single-step sampling triggered";
        }

        ImGui::TextWrapped("%s", runtime_ops_status.empty() ? "(no operation yet)" : runtime_ops_status.c_str());
    }

    ImGui::SeparatorText("Task Category");
    ImGui::Text("Primary: %s", TaskPrimaryCategoryNameUi(runtime.task_primary));
    ImGui::Text("Secondary: %s", TaskSecondaryCategoryNameUi(runtime.task_secondary));
}

}  // namespace k2d
