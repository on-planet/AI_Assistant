#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include <array>

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimeAsrPanel(RuntimeUiView view) {
    const AsrChatPanelState &panel_state = BuildAsrChatPanelState(view.runtime);

    ImGui::BeginChild("asr_status_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Status Card");
    ImGui::Text("ASR: %s (%s)",
                panel_state.asr_ready ? "ready" : "not ready",
                panel_state.feature_asr_enabled ? "enabled" : "disabled");
    std::string asr_text = panel_state.asr_text;
    RenderLongTextBlock("ASR Text", "asr_text_child", &asr_text, 10, 100.0f);
    ImGui::Text("Switch Reason: %s",
                panel_state.asr_switch_reason.empty() ? "(none)" : panel_state.asr_switch_reason.c_str());
    ImGui::Text("RTF: %.3f", panel_state.asr_rtf);
    ImGui::Text("WER Proxy: %.3f", panel_state.asr_wer_proxy);
    ImGui::Text("Timeout Rate: %.2f%%", panel_state.asr_timeout_rate * 100.0);
    ImGui::Text("Cloud Call Ratio: %.2f%%", panel_state.asr_cloud_call_ratio * 100.0);
    ImGui::Text("Cloud Success Ratio: %.2f%%", panel_state.asr_cloud_success_ratio * 100.0);
    if (!panel_state.asr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "ASR Error: %s", panel_state.asr_last_error.c_str());
    }
    ImGui::EndChild();
}

void RenderRuntimePluginWorkerPanel(RuntimeUiView view) {
    const AsrChatPanelState &panel_state = BuildAsrChatPanelState(view.runtime);

    ImGui::BeginChild("plugin_worker_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Plugin Worker");
    ImGui::Text("update_hz: %d", panel_state.plugin_update_hz);
    ImGui::Text("total updates: %llu", static_cast<unsigned long long>(panel_state.plugin_total_updates));
    ImGui::Text("timeout/exception/internal: %llu / %llu / %llu",
                static_cast<unsigned long long>(panel_state.plugin_timeout_count),
                static_cast<unsigned long long>(panel_state.plugin_exception_count),
                static_cast<unsigned long long>(panel_state.plugin_internal_error_count));
    ImGui::Text("timeout rate: %.2f%%", panel_state.plugin_timeout_rate * 100.0);
    ImGui::Text("disable/recover: %llu / %llu",
                static_cast<unsigned long long>(panel_state.plugin_disable_count),
                static_cast<unsigned long long>(panel_state.plugin_recover_count));
    ImGui::Text("auto_disabled: %s", panel_state.plugin_auto_disabled ? "true" : "false");
    if (panel_state.has_plugin_last_error) {
        ImGui::TextWrapped("Plugin Last Error: %s", panel_state.plugin_last_error.c_str());
    }

    ImGui::SeparatorText("Plugin Route Trace");
    ImGui::Text("Selected: %s", panel_state.plugin_route_selected.c_str());
    ImGui::Text("Scene Score: %.2f", panel_state.plugin_route_scene_score);
    ImGui::Text("Task Score: %.2f", panel_state.plugin_route_task_score);
    ImGui::Text("Presence Score: %.2f", panel_state.plugin_route_presence_score);
    ImGui::Text("Total Score: %.2f", panel_state.plugin_route_total_score);
    if (!panel_state.has_route_rejected_summary) {
        RenderUnifiedEmptyState("plugin_route_no_rejected_empty_state",
                                "No rejected routes",
                                "Rejected route details will appear here when routing filters out candidates.",
                                ImVec4(0.45f, 0.85f, 0.45f, 1.0f));
    } else {
        ImGui::BeginChild("plugin_route_trace_child", ImVec2(-1.0f, 88.0f), ImGuiChildFlags_Borders);
        for (const auto &line : panel_state.plugin_route_rejected_summary) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::EndChild();
}

void RenderRuntimeChatPanel(RuntimeUiView view) {
    AppRuntime &runtime = view.runtime;
    const AsrChatPanelState *panel_state = &BuildAsrChatPanelState(runtime);

    ImGui::BeginChild("asr_chat_interaction_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Param Card (Editable)");

    bool log_enabled = panel_state->observability_log_enabled;
    if (ImGui::Checkbox("Enable Periodic Observability Log", &log_enabled)) {
        ApplyAsrChatPanelAction(runtime,
                                AsrChatPanelAction{
                                    .type = AsrChatPanelActionType::SetObservabilityLogEnabled,
                                    .bool_value = log_enabled,
                                });
        panel_state = &BuildAsrChatPanelState(runtime);
    }

    float log_interval_sec = panel_state->observability_log_interval_sec;
    if (ImGui::SliderFloat("Log Interval (s)", &log_interval_sec, 0.2f, 10.0f, "%.1f")) {
        log_interval_sec = std::clamp(log_interval_sec, 0.2f, 10.0f);
        ApplyAsrChatPanelAction(runtime,
                                AsrChatPanelAction{
                                    .type = AsrChatPanelActionType::SetObservabilityLogIntervalSec,
                                    .float_value = log_interval_sec,
                                });
        panel_state = &BuildAsrChatPanelState(runtime);
    }

    ImGui::SeparatorText("Chat");

    bool chat_enabled = panel_state->feature_chat_enabled;
    if (ImGui::Checkbox("Enable Chat", &chat_enabled)) {
        ApplyAsrChatPanelAction(runtime,
                                AsrChatPanelAction{
                                    .type = AsrChatPanelActionType::SetChatEnabled,
                                    .bool_value = chat_enabled,
                                });
        panel_state = &BuildAsrChatPanelState(runtime);
    }

    bool prefer_cloud_chat = panel_state->prefer_cloud_chat;
    if (ImGui::Checkbox("Prefer Cloud Chat", &prefer_cloud_chat)) {
        ApplyAsrChatPanelAction(runtime,
                                AsrChatPanelAction{
                                    .type = AsrChatPanelActionType::SetPreferCloudChat,
                                    .bool_value = prefer_cloud_chat,
                                });
        panel_state = &BuildAsrChatPanelState(runtime);
    }

    ImGui::Text("Chat: %s", panel_state->chat_ready ? "ready" : "not ready");

    std::array<char, sizeof(runtime.chat_input)> chat_input{};
    SDL_strlcpy(chat_input.data(), panel_state->chat_input.c_str(), chat_input.size());
    if (ImGui::InputTextMultiline("##chat_input", chat_input.data(), chat_input.size(), ImVec2(-1.0f, 72.0f))) {
        ApplyAsrChatPanelAction(runtime,
                                AsrChatPanelAction{
                                    .type = AsrChatPanelActionType::SetChatInput,
                                    .text_value = chat_input.data(),
                                });
        panel_state = &BuildAsrChatPanelState(runtime);
    }

    if (ImGui::Button("Send Chat")) {
        if (!panel_state->can_send_chat) {
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
                if (rsp.fallback_to_offline || (rsp.cloud_attempted && !rsp.cloud_succeeded)) {
                    const std::string degrade_detail = rsp.switch_reason.empty()
                                                           ? std::string("chat degraded: cloud fallback to offline")
                                                           : std::string("chat degraded: ") + rsp.switch_reason;
                    UpdateRuntimeDegrade(runtime.chat_error_info,
                                         RuntimeErrorDomain::Chat,
                                         RuntimeErrorCode::TimeoutDegraded,
                                         degrade_detail);
                } else {
                    ClearRuntimeError(runtime.chat_error_info);
                }
            } else {
                runtime.chat_last_error = err;
                UpdateRuntimeError(runtime.chat_error_info,
                                   RuntimeErrorDomain::Chat,
                                   RuntimeErrorCode::InferenceFailed,
                                   runtime.chat_last_error);
            }
        }
        runtime.RuntimeAsrChatState::panel_state_version += 1;
        panel_state = &BuildAsrChatPanelState(runtime);
    }

    std::string chat_answer = panel_state->chat_answer;
    RenderLongTextBlock("Answer", "chat_answer_child", &chat_answer, 12, 120.0f);
    ImGui::Text("Switch Reason: %s",
                panel_state->chat_switch_reason.empty() ? "(none)" : panel_state->chat_switch_reason.c_str());
    if (!panel_state->chat_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Chat Error: %s", panel_state->chat_last_error.c_str());
    }

    RenderModuleLatestErrorCard(panel_state->recent_error);
    ImGui::EndChild();
}

}  // namespace desktoper2D
