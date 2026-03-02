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
    ImGui::Checkbox("Manual Param Mode", &runtime.manual_param_mode);
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
    if (runtime.perception_state.ocr_ready && !runtime.perception_state.ocr_result.summary.empty()) {
        ImGui::TextWrapped("OCR Summary: %s", runtime.perception_state.ocr_result.summary.c_str());
        if (!runtime.perception_state.ocr_result.lines.empty()) {
            ImGui::Text("OCR Top1: %s (%.3f)",
                        runtime.perception_state.ocr_result.lines.front().text.c_str(),
                        runtime.perception_state.ocr_result.lines.front().score);
        }
    }
    if (!runtime.perception_state.ocr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "OCR Error: %s", runtime.perception_state.ocr_last_error.c_str());
    }

    ImGui::SeparatorText("ASR");
    ImGui::Text("ASR: %s (%s)", runtime.asr_ready ? "ready" : "not ready", runtime.feature_asr_enabled ? "enabled" : "disabled");
    if (!runtime.asr_last_result.text.empty()) {
        ImGui::TextWrapped("ASR Text: %s", runtime.asr_last_result.text.c_str());
    }
    if (!runtime.asr_last_error.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "ASR Error: %s", runtime.asr_last_error.c_str());
    }

    ImGui::SeparatorText("Task Category");
    ImGui::Text("Primary: %s", TaskPrimaryCategoryNameUi(runtime.task_primary));
    ImGui::Text("Secondary: %s", TaskSecondaryCategoryNameUi(runtime.task_secondary));
}

}  // namespace k2d
