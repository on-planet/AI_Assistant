#include "k2d/lifecycle/ui/app_debug_ui.h"

#include <algorithm>
#include <string>

#include "imgui.h"

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

}  // namespace

void RenderRuntimeDebugSummary(const AppRuntime &runtime) {
    ImGui::Text("FPS: %.2f", runtime.debug_fps);
    ImGui::Text("Frame: %.2f ms", runtime.debug_frame_ms);
    ImGui::Text("Model Loaded: %s", runtime.model_loaded ? "Yes" : "No");
    ImGui::Text("PartCount: %d", static_cast<int>(runtime.model.parts.size()));
}

void RenderAppDebugUi(AppRuntime &runtime) {
    RenderRuntimeDebugSummary(runtime);

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

    ImGui::Separator();
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

    ImGui::SeparatorText("OCR");
    ImGui::SliderInt("OCR Timeout (ms)", &runtime.perception_state.ocr_timeout_ms, 500, 10000);
    runtime.perception_state.ocr_timeout_ms = std::clamp(runtime.perception_state.ocr_timeout_ms, 500, 10000);
    ImGui::SliderInt("OCR Det Input", &runtime.perception_state.ocr_det_input_size, 160, 1280);
    runtime.perception_state.ocr_det_input_size = std::clamp(runtime.perception_state.ocr_det_input_size, 160, 1280);

    if (runtime.perception_state.ocr_ready && !runtime.perception_state.ocr_result.summary.empty()) {
        ImGui::TextWrapped("OCR Summary: %s", runtime.perception_state.ocr_result.summary.c_str());
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

    ImGui::SeparatorText("Face Param Mapping");
    ImGui::Checkbox("Enable Face->Param Mapping", &runtime.feature_face_param_mapping_enabled);
    ImGui::SliderFloat("Map Min Confidence", &runtime.face_map_min_confidence, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Map Pose Deadzone (deg)", &runtime.face_map_head_pose_deadzone_deg, 0.0f, 15.0f, "%.1f");
    ImGui::SliderFloat("Map Yaw Max (deg)", &runtime.face_map_yaw_max_deg, 5.0f, 45.0f, "%.1f");
    ImGui::SliderFloat("Map Pitch Max (deg)", &runtime.face_map_pitch_max_deg, 5.0f, 35.0f, "%.1f");
    ImGui::SliderFloat("Map EyeOpen Threshold", &runtime.face_map_eye_open_threshold, 0.0f, 0.9f, "%.2f");
    ImGui::SliderFloat("Map Param Weight", &runtime.face_map_param_weight, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Map Smooth Alpha", &runtime.face_map_smooth_alpha, 0.0f, 1.0f, "%.2f");

    ImGui::Text("Gate: %s", runtime.face_map_gate_reason.empty() ? "(none)" : runtime.face_map_gate_reason.c_str());
    ImGui::Text("Raw Yaw/Pitch/Eye: %.2f / %.2f / %.2f",
                runtime.face_map_raw_yaw_deg,
                runtime.face_map_raw_pitch_deg,
                runtime.face_map_raw_eye_open);
    ImGui::Text("Mapped HeadYaw/HeadPitch/EyeOpen: %.2f / %.2f / %.2f",
                runtime.face_map_out_head_yaw,
                runtime.face_map_out_head_pitch,
                runtime.face_map_out_eye_open);

    ImGui::SeparatorText("ASR");
    ImGui::Text("ASR: %s (%s)", runtime.asr_ready ? "ready" : "not ready", runtime.feature_asr_enabled ? "enabled" : "disabled");
    if (!runtime.asr_last_result.text.empty()) {
        ImGui::TextWrapped("ASR Text: %s", runtime.asr_last_result.text.c_str());
    }
    ImGui::Text("Switch Reason: %s", runtime.asr_last_switch_reason.empty() ? "(none)" : runtime.asr_last_switch_reason.c_str());
    ImGui::Text("RTF: %.3f", runtime.asr_rtf);
    ImGui::Text("WER Proxy: %.3f", runtime.asr_wer_proxy);
    ImGui::Text("Timeout Rate: %.2f%%", runtime.asr_timeout_rate * 100.0);
    ImGui::Text("Cloud Call Ratio: %.2f%%", runtime.asr_cloud_call_ratio * 100.0);
    ImGui::Text("Cloud Success Ratio: %.2f%%", runtime.asr_cloud_success_ratio * 100.0);
    if (!runtime.asr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "ASR Error: %s", runtime.asr_last_error.c_str());
    }

    ImGui::SeparatorText("Chat");
    ImGui::Checkbox("Enable Chat", &runtime.feature_chat_enabled);
    ImGui::Checkbox("Prefer Cloud Chat", &runtime.prefer_cloud_chat);
    ImGui::Text("Chat: %s", runtime.chat_ready ? "ready" : "not ready");
    ImGui::InputTextMultiline("##chat_input", runtime.chat_input, sizeof(runtime.chat_input), ImVec2(-1.0f, 72.0f));
    if (ImGui::Button("Send Chat")) {
        if (!(runtime.feature_chat_enabled && runtime.chat_ready && runtime.chat_provider)) {
            runtime.chat_last_error = "chat provider unavailable";
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
            } else {
                runtime.chat_last_error = err;
            }
        }
    }
    if (!runtime.chat_last_answer.empty()) {
        ImGui::TextWrapped("Answer: %s", runtime.chat_last_answer.c_str());
    }
    ImGui::Text("Switch Reason: %s", runtime.chat_last_switch_reason.empty() ? "(none)" : runtime.chat_last_switch_reason.c_str());
    if (!runtime.chat_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Chat Error: %s", runtime.chat_last_error.c_str());
    }

    ImGui::SeparatorText("Task Category");
    ImGui::Text("Primary: %s", TaskPrimaryCategoryNameUi(runtime.task_primary));
    ImGui::Text("Secondary: %s", TaskSecondaryCategoryNameUi(runtime.task_secondary));
}

}  // namespace k2d
