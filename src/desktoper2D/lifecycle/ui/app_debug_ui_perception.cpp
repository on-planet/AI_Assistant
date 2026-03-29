#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimePerceptionPanel(RuntimeUiView view) {
    AppRuntime &runtime = view.runtime;
    const PerceptionPanelState *panel_state = &BuildPerceptionPanelState(runtime);

    ImGui::BeginChild("perception_status_child", ImVec2(-1.0f, 210.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Status Card");
    ImGui::Text("Capture: %s", panel_state->capture_ready ? "ready" : "not ready");
    if (!panel_state->capture_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Capture Error: %s", panel_state->capture_error.c_str());
    }
    ImGui::Text("Scene Classifier: %s", panel_state->scene_ready ? "ready" : "not ready");
    if (panel_state->scene_ready && !panel_state->scene_label.empty()) {
        ImGui::Text("Scene: %s (%.3f)", panel_state->scene_label.c_str(), panel_state->scene_score);
    }
    if (!panel_state->scene_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Scene Error: %s", panel_state->scene_error.c_str());
    }

    ImGui::Text("Camera FaceMesh: %s (%s)",
                panel_state->facemesh_ready ? "ready" : "not ready",
                panel_state->feature_face_emotion_enabled ? "enabled" : "disabled");
    if (panel_state->facemesh_ready) {
        ImGui::Text("Face: %s", panel_state->face_detected ? "present" : "none");
        ImGui::Text("Emotion: %s (%.2f)",
                    panel_state->emotion_label.empty() ? "(none)" : panel_state->emotion_label.c_str(),
                    panel_state->emotion_score);
        ImGui::Text("HeadPose Y/P/R (deg): %.2f / %.2f / %.2f",
                    panel_state->head_yaw_deg,
                    panel_state->head_pitch_deg,
                    panel_state->head_roll_deg);
        ImGui::Text("Eye Open L/R/Avg: %.2f / %.2f / %.2f",
                    panel_state->eye_open_left,
                    panel_state->eye_open_right,
                    panel_state->eye_open_avg);
        ImGui::Text("Keypoints: %d", panel_state->face_keypoint_count);
        if (panel_state->has_face_keypoints) {
            ImGui::Text("KP[0]: %s (%.1f, %.1f) s=%.2f",
                        panel_state->first_keypoint_name.c_str(),
                        panel_state->first_keypoint_x,
                        panel_state->first_keypoint_y,
                        panel_state->first_keypoint_score);
        }
    }
    if (!panel_state->camera_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Camera Error: %s", panel_state->camera_error.c_str());
    }

    ImGui::SeparatorText("System Context");
    ImGui::Text("Process: %s", panel_state->process_name.empty() ? "(empty)" : panel_state->process_name.c_str());
    ImGui::TextWrapped("Title: %s", panel_state->window_title.empty() ? "(empty)" : panel_state->window_title.c_str());
    ImGui::TextWrapped("URL: %s", panel_state->url_hint.empty() ? "(empty)" : panel_state->url_hint.c_str());
    if (!panel_state->system_context_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                           "System Context Error: %s",
                           panel_state->system_context_error.c_str());
    }
    ImGui::EndChild();

    ImGui::BeginChild("perception_metrics_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Perception Performance");

    float capture_poll_interval_sec = panel_state->capture_poll_interval_sec;
    if (ImGui::SliderFloat("Capture Poll Interval (s)", &capture_poll_interval_sec, 0.1f, 5.0f, "%.2f")) {
        capture_poll_interval_sec = std::clamp(capture_poll_interval_sec, 0.1f, 5.0f);
        ApplyPerceptionPanelAction(runtime,
                                   PerceptionPanelAction{
                                       .type = PerceptionPanelActionType::SetCapturePollIntervalSec,
                                       .float_value = capture_poll_interval_sec,
                                   });
        panel_state = &BuildPerceptionPanelState(runtime);
    }

    ImGui::Text("Capture Success/Fail: %lld / %lld (%.1f%%)",
                static_cast<long long>(panel_state->capture_success_count),
                static_cast<long long>(panel_state->capture_fail_count),
                panel_state->capture_success_rate * 100.0);
    ImGui::Text("Scene Avg Latency: %.1f ms (runs=%lld)",
                panel_state->scene_avg_latency_ms,
                static_cast<long long>(panel_state->scene_total_runs));
    ImGui::Text("Face Avg Latency: %.1f ms (runs=%lld)",
                panel_state->face_avg_latency_ms,
                static_cast<long long>(panel_state->face_total_runs));

    RenderModuleLatestErrorCard(panel_state->recent_error);
    ImGui::EndChild();
}

void RenderRuntimeOcrPanel(RuntimeUiView view) {
    AppRuntime &runtime = view.runtime;
    const PerceptionPanelState *panel_state = &BuildPerceptionPanelState(runtime);

    ImGui::BeginChild("perception_ocr_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("OCR Status");
    ImGui::Text("OCR: %s (%s)",
                panel_state->ocr_ready ? "ready" : "not ready",
                panel_state->feature_ocr_enabled ? "enabled" : "disabled");
    if (!panel_state->ocr_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "OCR Error: %s", panel_state->ocr_error.c_str());
    }

    ImGui::SeparatorText("Param Card (Editable)");

    int ocr_timeout_ms = panel_state->ocr_timeout_ms;
    if (ImGui::SliderInt("OCR Timeout (ms)", &ocr_timeout_ms, 500, 10000)) {
        ocr_timeout_ms = std::clamp(ocr_timeout_ms, 500, 10000);
        ApplyPerceptionPanelAction(runtime,
                                   PerceptionPanelAction{
                                       .type = PerceptionPanelActionType::SetOcrTimeoutMs,
                                       .int_value = ocr_timeout_ms,
                                   });
        panel_state = &BuildPerceptionPanelState(runtime);
    }

    int ocr_det_input_size = panel_state->ocr_det_input_size;
    if (ImGui::SliderInt("OCR Det Input", &ocr_det_input_size, 160, 1280)) {
        ocr_det_input_size = std::clamp(ocr_det_input_size, 160, 1280);
        ApplyPerceptionPanelAction(runtime,
                                   PerceptionPanelAction{
                                       .type = PerceptionPanelActionType::SetOcrDetInputSize,
                                       .int_value = ocr_det_input_size,
                                   });
        panel_state = &BuildPerceptionPanelState(runtime);
    }
    ImGui::Text("OCR Det Effective Input: %d", panel_state->ocr_det_effective_input_size);

    if (panel_state->ocr_ready) {
        std::string ocr_summary = panel_state->ocr_summary;
        RenderLongTextBlock("OCR Summary", "ocr_summary_child", &ocr_summary, 8, 100.0f);
        if (panel_state->has_ocr_lines) {
            ImGui::Text("OCR Top1: %s (%.3f)", panel_state->top_ocr_text.c_str(), panel_state->top_ocr_score);
        }
    }

    ImGui::SeparatorText("OCR Quality Metrics");
    ImGui::Text("Avg Latency: %.1f ms", panel_state->ocr_avg_latency_ms);
    ImGui::Text("Discard Rate: %.2f%%", panel_state->ocr_discard_rate * 100.0f);
    ImGui::Text("Kept / Raw Lines: %lld / %lld",
                static_cast<long long>(panel_state->ocr_total_kept_lines),
                static_cast<long long>(panel_state->ocr_total_raw_lines));
    ImGui::Text("Dropped Low-Conf Lines: %lld (threshold=%.2f)",
                static_cast<long long>(panel_state->ocr_total_dropped_low_conf_lines),
                panel_state->ocr_low_conf_threshold);

    if (panel_state->has_confidence_samples) {
        ImGui::Text("Confidence Dist [<0.5 / 0.5~0.8 / >=0.8]: %.1f%% / %.1f%% / %.1f%%",
                    panel_state->ocr_conf_low_pct,
                    panel_state->ocr_conf_mid_pct,
                    panel_state->ocr_conf_high_pct);
    } else {
        RenderUnifiedEmptyState("perception_confidence_no_samples_empty_state",
                                "No samples yet",
                                "Wait for OCR results before reviewing confidence distribution.",
                                ImVec4(0.72f, 0.82f, 1.0f, 1.0f));
    }

    ImGui::EndChild();
}

}  // namespace desktoper2D
