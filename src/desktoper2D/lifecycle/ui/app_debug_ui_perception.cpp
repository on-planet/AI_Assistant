#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimePerceptionPanel(AppRuntime &runtime) {
    ImGui::BeginChild("perception_status_child", ImVec2(-1.0f, 210.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Status Card");
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

    ImGui::Text("Camera FaceMesh: %s (%s)", runtime.perception_state.camera_facemesh_ready ? "ready" : "not ready", runtime.feature_flags.face_emotion_enabled ? "enabled" : "disabled");
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
    ImGui::EndChild();

    ImGui::BeginChild("perception_metrics_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
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

    const std::string &perception_recent_error =
        !runtime.perception_state.camera_facemesh_last_error.empty() ? runtime.perception_state.camera_facemesh_last_error :
        !runtime.perception_state.scene_classifier_last_error.empty() ? runtime.perception_state.scene_classifier_last_error :
        !runtime.perception_state.system_context_last_error.empty() ? runtime.perception_state.system_context_last_error :
        runtime.perception_state.screen_capture_last_error;
    RenderModuleLatestErrorCard(perception_recent_error);
    ImGui::EndChild();
}

void RenderRuntimeOcrPanel(AppRuntime &runtime) {
    ImGui::BeginChild("perception_ocr_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("OCR Status");
    ImGui::Text("OCR: %s (%s)", runtime.perception_state.ocr_ready ? "ready" : "not ready", runtime.feature_flags.ocr_enabled ? "enabled" : "disabled");
    if (!runtime.perception_state.ocr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "OCR Error: %s", runtime.perception_state.ocr_last_error.c_str());
    }

    ImGui::SeparatorText("Param Card (Editable)");
    ImGui::SliderInt("OCR Timeout (ms)", &runtime.perception_state.ocr_timeout_ms, 500, 10000);
    runtime.perception_state.ocr_timeout_ms = std::clamp(runtime.perception_state.ocr_timeout_ms, 500, 10000);
    ImGui::SliderInt("OCR Det Input", &runtime.perception_state.ocr_det_input_size, 160, 1280);
    runtime.perception_state.ocr_det_input_size = std::clamp(runtime.perception_state.ocr_det_input_size, 160, 1280);
    ImGui::Text("OCR Det Effective Input: %d", runtime.perception_state.ocr_det_input_size);

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
    ImGui::Text("Det Preprocess Avg: %.1f ms | Det ONNX Avg: %.1f ms",
                runtime.perception_state.ocr_preprocess_det_avg_ms,
                runtime.perception_state.ocr_infer_det_avg_ms);
    ImGui::Text("Rec Preprocess Avg: %.1f ms | Rec ONNX Avg: %.1f ms",
                runtime.perception_state.ocr_preprocess_rec_avg_ms,
                runtime.perception_state.ocr_infer_rec_avg_ms);
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
        RenderUnifiedEmptyState("perception_confidence_no_samples_empty_state",
                                "无采样",
                                "当前还没有 OCR 置信度样本，请等待识别结果产生后再查看分布统计。",
                                ImVec4(0.72f, 0.82f, 1.0f, 1.0f));
    }

    ImGui::EndChild();
}


}  // namespace desktoper2D
