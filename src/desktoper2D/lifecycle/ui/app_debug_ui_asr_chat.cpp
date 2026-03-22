#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimeAsrPanel(AppRuntime &runtime) {
    ImGui::BeginChild("asr_status_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Status Card");
    ImGui::Text("ASR: %s (%s)", runtime.asr_ready ? "ready" : "not ready", runtime.feature_flags.asr_enabled ? "enabled" : "disabled");
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
    ImGui::EndChild();
}

void RenderRuntimePluginWorkerPanel(AppRuntime &runtime) {
    ImGui::BeginChild("plugin_worker_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Plugin Worker");
    ImGui::Text("update_hz: %d", runtime.plugin.current_update_hz);
    ImGui::Text("total updates: %llu", static_cast<unsigned long long>(runtime.plugin.total_update_count));
    ImGui::Text("timeout/exception/internal: %llu / %llu / %llu",
                static_cast<unsigned long long>(runtime.plugin.timeout_count),
                static_cast<unsigned long long>(runtime.plugin.exception_count),
                static_cast<unsigned long long>(runtime.plugin.internal_error_count));
    ImGui::Text("timeout rate: %.2f%%", runtime.plugin.timeout_rate * 100.0);
    ImGui::Text("disable/recover: %llu / %llu",
                static_cast<unsigned long long>(runtime.plugin.disable_count),
                static_cast<unsigned long long>(runtime.plugin.recover_count));
    ImGui::Text("auto_disabled: %s", runtime.plugin.auto_disabled ? "true" : "false");
    if (!runtime.plugin.last_error.empty()) {
        ImGui::TextWrapped("Plugin Last Error: %s", runtime.plugin.last_error.c_str());
    }

    ImGui::SeparatorText("Plugin Route Trace");
    ImGui::Text("Selected: %s", runtime.plugin.route_selected.c_str());
    ImGui::Text("Scene Score: %.2f", runtime.plugin.route_scene_score);
    ImGui::Text("Task Score: %.2f", runtime.plugin.route_task_score);
    ImGui::Text("Presence Score: %.2f", runtime.plugin.route_presence_score);
    ImGui::Text("Total Score: %.2f", runtime.plugin.route_total_score);
    if (runtime.plugin.route_rejected_summary.empty()) {
        RenderUnifiedEmptyState("plugin_route_no_rejected_empty_state",
                                "无拒绝记录",
                                "当前没有被拒绝的路由候选，说明候选链路较干净或尚未产生对比结果。",
                                ImVec4(0.45f, 0.85f, 0.45f, 1.0f));
    } else {
        ImGui::BeginChild("plugin_route_trace_child", ImVec2(-1.0f, 88.0f), ImGuiChildFlags_Borders);
        for (const auto &line : runtime.plugin.route_rejected_summary) {
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::EndChild();
}

void RenderRuntimeChatPanel(AppRuntime &runtime) {
    ImGui::BeginChild("asr_chat_interaction_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Param Card (Editable)");
    ImGui::Checkbox("Enable Periodic Observability Log", &runtime.observability.log_enabled);
    ImGui::SliderFloat("Log Interval (s)", &runtime.observability.log_interval_sec, 0.2f, 10.0f, "%.1f");
    runtime.observability.log_interval_sec =
        std::clamp(runtime.observability.log_interval_sec, 0.2f, 10.0f);

    ImGui::SeparatorText("Chat");
    ImGui::Checkbox("Enable Chat", &runtime.feature_flags.chat_enabled);
    ImGui::Checkbox("Prefer Cloud Chat", &runtime.prefer_cloud_chat);
    ImGui::Text("Chat: %s", runtime.chat_ready ? "ready" : "not ready");
    ImGui::InputTextMultiline("##chat_input", runtime.chat_input, sizeof(runtime.chat_input), ImVec2(-1.0f, 72.0f));
    if (ImGui::Button("Send Chat")) {
        if (!(runtime.feature_flags.chat_enabled && runtime.chat_ready && runtime.chat_provider)) {
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
                    std::string degrade_detail = rsp.switch_reason.empty()
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
    }
    RenderLongTextBlock("Answer", "chat_answer_child", &runtime.chat_last_answer, 12, 120.0f);
    ImGui::Text("Switch Reason: %s", runtime.chat_last_switch_reason.empty() ? "(none)" : runtime.chat_last_switch_reason.c_str());
    if (!runtime.chat_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Chat Error: %s", runtime.chat_last_error.c_str());
    }

    const std::string &asr_chat_recent_error =
        !runtime.chat_last_error.empty() ? runtime.chat_last_error :
        !runtime.asr_last_error.empty() ? runtime.asr_last_error :
        runtime.plugin.last_error;
    RenderModuleLatestErrorCard(asr_chat_recent_error);
    ImGui::EndChild();
}


}  // namespace desktoper2D
