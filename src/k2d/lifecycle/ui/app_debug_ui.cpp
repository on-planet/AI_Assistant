#include "k2d/lifecycle/ui/app_debug_ui.h"

#include <algorithm>
#include <array>
#include <ctime>
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
