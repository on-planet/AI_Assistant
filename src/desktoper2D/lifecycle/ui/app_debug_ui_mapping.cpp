#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimeMappingPanel(AppRuntime &runtime) {
    ImGui::BeginChild("mapping_status_child", ImVec2(-1.0f, 140.0f), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Status Card");
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

    ImGui::EndChild();

    ImGui::BeginChild("mapping_config_child", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_Borders);
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
    ImGui::EndChild();
}


}  // namespace desktoper2D
