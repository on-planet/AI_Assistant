#include "app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimeTimelinePanel(AppRuntime &runtime) {
    TimelinePanelState panel_state = BuildTimelinePanelState(runtime);

    bool timeline_enabled = panel_state.form.timeline_enabled;
    if (ImGui::Checkbox("Enable Timeline", &timeline_enabled)) {
        ApplyTimelinePanelAction(runtime,
                                 TimelinePanelAction{.type = TimelinePanelActionType::SetEnabled,
                                                     .bool_value = timeline_enabled});
        panel_state = BuildTimelinePanelState(runtime);
    }

    float cursor_sec = panel_state.form.cursor_sec;
    if (ImGui::SliderFloat("Timeline Cursor (s)", &cursor_sec, 0.0f, std::max(0.1f, panel_state.form.duration_sec), "%.2f")) {
        ApplyTimelinePanelAction(runtime,
                                 TimelinePanelAction{.type = TimelinePanelActionType::SetCursorSec,
                                                     .float_value = cursor_sec});
        panel_state = BuildTimelinePanelState(runtime);
    }

    float duration_sec = panel_state.form.duration_sec;
    if (ImGui::SliderFloat("Timeline Duration (s)", &duration_sec, 0.5f, 30.0f, "%.1f")) {
        ApplyTimelinePanelAction(runtime,
                                 TimelinePanelAction{.type = TimelinePanelActionType::SetDurationSec,
                                                     .float_value = duration_sec});
        panel_state = BuildTimelinePanelState(runtime);
    }

    bool snap_enabled = panel_state.form.snap_enabled;
    if (ImGui::Checkbox("Enable Snap", &snap_enabled)) {
        ApplyTimelinePanelAction(runtime,
                                 TimelinePanelAction{.type = TimelinePanelActionType::SetSnapEnabled,
                                                     .bool_value = snap_enabled});
        panel_state = BuildTimelinePanelState(runtime);
    }

    const char *snap_modes[] = {"整数帧", "0.1s", "播放头"};
    int snap_mode = panel_state.form.snap_mode;
    if (ImGui::Combo("Snap Mode", &snap_mode, snap_modes, 3)) {
        ApplyTimelinePanelAction(runtime,
                                 TimelinePanelAction{.type = TimelinePanelActionType::SetSnapMode,
                                                     .int_value = snap_mode});
        panel_state = BuildTimelinePanelState(runtime);
    }
    if (panel_state.form.snap_mode == 0) {
        float snap_fps = panel_state.form.snap_fps;
        if (ImGui::SliderFloat("Snap FPS", &snap_fps, 1.0f, 120.0f, "%.0f")) {
            ApplyTimelinePanelAction(runtime,
                                     TimelinePanelAction{.type = TimelinePanelActionType::SetSnapFps,
                                                         .float_value = snap_fps});
            panel_state = BuildTimelinePanelState(runtime);
        }
    }

        if (!panel_state.view.has_model_params) {
            RenderUnifiedEmptyState("timeline_no_params_empty_state",
                                    "无参数",
                                    "时间轴需要至少一个参数通道才能进行预览与关键帧编辑。",
                                    ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
        } else {
            if (panel_state.view.can_add_channel && ImGui::Button("Add Channel")) {
                ApplyTimelinePanelAction(runtime,
                                         TimelinePanelAction{.type = TimelinePanelActionType::AddChannel});
            }

            if (panel_state.view.has_channels) {
                auto refresh_panel_state = [&]() {
                    panel_state = BuildTimelinePanelState(runtime);
                };

                refresh_panel_state();
                const int selected_channel_index = std::clamp(panel_state.form.selected_channel_index,
                                                              0,
                                                              static_cast<int>(panel_state.view.channel_options.size()) - 1);
                const std::string channel_label = panel_state.view.active_channel.id;

                if (ImGui::BeginCombo("Channel", channel_label.c_str())) {
                    for (const auto &channel_option : panel_state.view.channel_options) {
                        const bool sel = channel_option.value == selected_channel_index;
                        if (ImGui::Selectable(channel_option.label.c_str(), sel)) {
                            ApplyTimelinePanelAction(runtime,
                                                     TimelinePanelAction{.type = TimelinePanelActionType::SelectChannel,
                                                                         .int_value = channel_option.value});
                            refresh_panel_state();
                        }
                    }
                    ImGui::EndCombo();
                }

                bool channel_enabled = panel_state.view.active_channel.enabled;
                if (ImGui::Checkbox("Channel Enabled", &channel_enabled)) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::SetChannelEnabled,
                                                                 .bool_value = channel_enabled});
                    refresh_panel_state();
                }
                const char *interp_items[] = {"Step", "Linear", "Hermite"};
                int interp_idx = panel_state.view.active_channel.interpolation == TimelineInterpolation::Step ? 0 :
                                 (panel_state.view.active_channel.interpolation == TimelineInterpolation::Linear ? 1 : 2);
                if (ImGui::Combo("Interpolation", &interp_idx, interp_items, 3)) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::SetChannelInterpolation,
                                                                 .int_value = interp_idx});
                    refresh_panel_state();
                }

                const char *wrap_items[] = {"Clamp", "Loop", "PingPong"};
                int wrap_idx = panel_state.view.active_channel.wrap == TimelineWrapMode::Clamp ? 0 :
                               (panel_state.view.active_channel.wrap == TimelineWrapMode::Loop ? 1 : 2);
                if (ImGui::Combo("Wrap", &wrap_idx, wrap_items, 3)) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::SetChannelWrap,
                                                                 .int_value = wrap_idx});
                    refresh_panel_state();
                }

                int param_idx = std::clamp(panel_state.view.active_channel.param_index, 0, static_cast<int>(panel_state.view.param_options.size()) - 1);
                const std::string param_label = panel_state.view.param_options[static_cast<std::size_t>(param_idx)].label;
                if (ImGui::BeginCombo("Target Param", param_label.c_str())) {
                    for (const auto &param_option : panel_state.view.param_options) {
                        const bool sel = param_option.value == param_idx;
                        if (ImGui::Selectable(param_option.label.c_str(), sel)) {
                            ApplyTimelinePanelAction(runtime,
                                                     TimelinePanelAction{.type = TimelinePanelActionType::SetChannelTargetParam,
                                                                         .int_value = param_option.value});
                            refresh_panel_state();
                            param_idx = std::clamp(panel_state.view.active_channel.param_index,
                                                   0,
                                                   static_cast<int>(panel_state.view.param_options.size()) - 1);
                        }
                    }
                    ImGui::EndCombo();
                }

                if (panel_state.view.can_add_or_update_keyframe && ImGui::Button("Add/Update Keyframe At Cursor")) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::AddOrUpdateKeyframeAtCursor});
                    refresh_panel_state();
                }
                ImGui::SameLine();
                if (panel_state.view.can_copy_selected_keyframes && ImGui::Button("Copy Selected Keyframes")) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::CopySelectedKeyframes});
                }
                ImGui::SameLine();
                if (panel_state.view.can_paste && ImGui::Button("Paste At Cursor")) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::PasteAtCursor});
                    refresh_panel_state();
                }
                ImGui::SameLine();
                if (panel_state.view.can_remove_last_keyframe && ImGui::Button("Remove Last Keyframe")) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::RemoveLastKeyframe});
                    refresh_panel_state();
                }
                ImGui::SameLine();
                if (panel_state.view.can_delete_channel && ImGui::Button("Delete Channel")) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::DeleteChannel});
                }

                if (ImGui::Button("Undo") && panel_state.view.can_undo) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::Undo});
                    refresh_panel_state();
                }
                ImGui::SameLine();
                if (ImGui::Button("Redo") && panel_state.view.can_redo) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::Redo});
                    refresh_panel_state();
                }
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
                    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && panel_state.view.can_undo) {
                        ApplyTimelinePanelAction(runtime,
                                                 TimelinePanelAction{.type = TimelinePanelActionType::Undo});
                        refresh_panel_state();
                    }
                    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && panel_state.view.can_redo) {
                        ApplyTimelinePanelAction(runtime,
                                                 TimelinePanelAction{.type = TimelinePanelActionType::Redo});
                        refresh_panel_state();
                    }
                }

                ImGui::Text("Keyframes: %d | Selected: %d | Clipboard: %d | Undo: %d | Redo: %d",
                            static_cast<int>(panel_state.view.active_channel.keyframes.size()),
                            panel_state.view.selected_keyframe_count,
                            panel_state.view.clipboard_keyframe_count,
                            panel_state.view.undo_count,
                            panel_state.view.redo_count);

                ImGui::SeparatorText("Track Editor (Drag)");
                const auto &kfs = panel_state.view.active_channel.keyframes;
                const float track_h = 180.0f;
                const ImVec2 track_size(ImGui::GetContentRegionAvail().x, track_h);
                const ImVec2 track_pos = ImGui::GetCursorScreenPos();
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImGui::InvisibleButton("##timeline_track_drag", track_size,
                                       ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

                const float duration = std::max(0.001f, panel_state.view.duration_sec);
                const float vmin = panel_state.view.active_param_min_value;
                const float vmax = panel_state.view.active_param_max_value;

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

                const float cursor_x = time_to_x(panel_state.view.cursor_sec);
                dl->AddLine(ImVec2(cursor_x, track_pos.y),
                            ImVec2(cursor_x, track_pos.y + track_size.y),
                            IM_COL32(255, 204, 96, 220),
                            1.5f);

                const bool hovered = ImGui::IsItemHovered();
                const ImVec2 mouse = ImGui::GetIO().MousePos;

                auto is_kf_selected = [&](int idx) {
                    return std::find(panel_state.view.selected_keyframe_indices.begin(),
                                     panel_state.view.selected_keyframe_indices.end(),
                                     idx) != panel_state.view.selected_keyframe_indices.end();
                };

                for (std::size_t i = 1; i < kfs.size(); ++i) {
                    const auto &a = kfs[i - 1];
                    const auto &b = kfs[i];
                    dl->AddLine(ImVec2(time_to_x(a.time_sec), value_to_y(a.value)),
                                ImVec2(time_to_x(b.time_sec), value_to_y(b.value)),
                                IM_COL32(120, 200, 255, 230),
                                1.8f);
                }

                int hovered_kf_idx = -1;
                for (std::size_t i = 0; i < kfs.size(); ++i) {
                    const auto &kf = kfs[i];
                    const ImVec2 p(time_to_x(kf.time_sec), value_to_y(kf.value));
                    const float dx = mouse.x - p.x;
                    const float dy = mouse.y - p.y;
                    if (dx * dx + dy * dy <= 64.0f) {
                        hovered_kf_idx = static_cast<int>(i);
                    }
                }

                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_kf_idx >= 0) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::SelectKeyframe,
                                                                 .int_value = hovered_kf_idx,
                                                                 .additive = ImGui::GetIO().KeyShift});
                } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_kf_idx < 0) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::BeginBoxSelect,
                                                                 .float_value = mouse.x,
                                                                 .float_value2 = mouse.y,
                                                                 .additive = ImGui::GetIO().KeyShift});
                }
                if (panel_state.view.box_select_active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::UpdateBoxSelect,
                                                                 .float_value = mouse.x,
                                                                 .float_value2 = mouse.y});
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    if (panel_state.view.box_select_active) {
                        ApplyTimelinePanelAction(runtime,
                                                 TimelinePanelAction{.type = TimelinePanelActionType::EndBoxSelect,
                                                                     .int_value = static_cast<int>(track_size.x),
                                                                     .int_value2 = static_cast<int>(track_size.y),
                                                                     .float_value = track_pos.x,
                                                                     .float_value2 = track_pos.y});
                    } else {
                        ApplyTimelinePanelAction(runtime,
                                                 TimelinePanelAction{.type = TimelinePanelActionType::EndTrackDrag});
                        refresh_panel_state();
                    }
                }

                if (panel_state.view.dragging_channel_index == panel_state.view.selected_channel_index &&
                    panel_state.view.dragging_keyframe_index >= 0 &&
                    panel_state.view.dragging_keyframe_index < static_cast<int>(kfs.size()) &&
                    ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (!panel_state.view.drag_snapshot_captured) {
                        ApplyTimelinePanelAction(runtime,
                                                 TimelinePanelAction{.type = TimelinePanelActionType::BeginTrackDrag,
                                                                     .int_value = panel_state.view.dragging_keyframe_index});
                    }
                    ApplyTimelinePanelAction(runtime,
                                             TimelinePanelAction{.type = TimelinePanelActionType::UpdateTrackDrag,
                                                                 .int_value = panel_state.view.dragging_keyframe_index,
                                                                 .float_value = x_to_time(mouse.x),
                                                                 .float_value2 = y_to_value(mouse.y)});
                    refresh_panel_state();
                }


                if (panel_state.view.box_select_active) {
                    const ImVec2 sel_min(std::min(panel_state.view.box_select_start_x, panel_state.view.box_select_end_x),
                                         std::min(panel_state.view.box_select_start_y, panel_state.view.box_select_end_y));
                    const ImVec2 sel_max(std::max(panel_state.view.box_select_start_x, panel_state.view.box_select_end_x),
                                         std::max(panel_state.view.box_select_start_y, panel_state.view.box_select_end_y));
                    dl->AddRectFilled(sel_min, sel_max, IM_COL32(120, 180, 255, 36));
                    dl->AddRect(sel_min, sel_max, IM_COL32(120, 180, 255, 180), 0.0f, 0, 1.0f);
                }

                for (std::size_t i = 0; i < kfs.size(); ++i) {
                    const auto &kf = kfs[i];
                    const ImVec2 p(time_to_x(kf.time_sec), value_to_y(kf.value));
                    const bool active = panel_state.view.dragging_channel_index == panel_state.view.selected_channel_index &&
                                        panel_state.view.dragging_keyframe_index == static_cast<int>(i);
                    const bool selected = is_kf_selected(static_cast<int>(i));
                    const ImU32 fill = active ? IM_COL32(255, 190, 70, 255)
                                              : (selected ? IM_COL32(120, 220, 140, 255) : IM_COL32(210, 230, 255, 255));
                    dl->AddCircleFilled(p, active ? 6.0f : (selected ? 5.5f : 4.5f), fill);
                    dl->AddCircle(p, active ? 6.0f : (selected ? 5.5f : 4.5f), IM_COL32(20, 24, 30, 255), 0, 1.0f);
                }

                if (hovered && hovered_kf_idx >= 0) {
                    const auto &hkf = kfs[static_cast<std::size_t>(hovered_kf_idx)];
                    ImGui::SetTooltip("KF[%d]\nt=%.3f\nv=%.3f\nselected=%s",
                                      hovered_kf_idx,
                                      hkf.time_sec,
                                      hkf.value,
                                      is_kf_selected(hovered_kf_idx) ? "yes" : "no");
                }
                ImGui::TextDisabled("左键点选；Shift+左键追加选择；空白区域拖拽框选关键帧");

                for (std::size_t i = 0; i < kfs.size(); ++i) {
                    const auto &kf = kfs[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::Text("t=%.2f v=%.3f", kf.time_sec, kf.value);
                    if (panel_state.view.active_channel.interpolation == TimelineInterpolation::Hermite) {
                        float in_tangent = kf.in_tangent;
                        float out_tangent = kf.out_tangent;
                        float in_weight = kf.in_weight;
                        float out_weight = kf.out_weight;
                        if (ImGui::SliderFloat("inTan", &in_tangent, -20.0f, 20.0f, "%.3f")) {
                            ApplyTimelinePanelAction(runtime,
                                                     TimelinePanelAction{.type = TimelinePanelActionType::SetKeyframeInTangent,
                                                                         .float_value = in_tangent,
                                                                         .stable_id = kf.stable_id});
                            refresh_panel_state();
                        }
                        if (ImGui::SliderFloat("outTan", &out_tangent, -20.0f, 20.0f, "%.3f")) {
                            ApplyTimelinePanelAction(runtime,
                                                     TimelinePanelAction{.type = TimelinePanelActionType::SetKeyframeOutTangent,
                                                                         .float_value = out_tangent,
                                                                         .stable_id = kf.stable_id});
                            refresh_panel_state();
                        }
                        if (ImGui::SliderFloat("inWeight", &in_weight, 0.0f, 1.0f, "%.3f")) {
                            ApplyTimelinePanelAction(runtime,
                                                     TimelinePanelAction{.type = TimelinePanelActionType::SetKeyframeInWeight,
                                                                         .float_value = in_weight,
                                                                         .stable_id = kf.stable_id});
                            refresh_panel_state();
                        }
                        if (ImGui::SliderFloat("outWeight", &out_weight, 0.0f, 1.0f, "%.3f")) {
                            ApplyTimelinePanelAction(runtime,
                                                     TimelinePanelAction{.type = TimelinePanelActionType::SetKeyframeOutWeight,
                                                                         .float_value = out_weight,
                                                                         .stable_id = kf.stable_id});
                            refresh_panel_state();
                        }
                    }
                    ImGui::PopID();
                }
            } else {
                RenderUnifiedEmptyState("timeline_no_keyframes_empty_state",
                                        "无关键帧",
                                        "当前还没有时间轴通道或关键帧，请先添加通道并在播放头位置写入关键帧。",
                                        ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
            }
    }
}


}  // namespace desktoper2D
