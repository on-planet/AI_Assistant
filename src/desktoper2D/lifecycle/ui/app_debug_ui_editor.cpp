#include "app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {

void RenderRuntimeEditorPanel(AppRuntime &runtime) {
    EditorPanelState panel_state = BuildEditorPanelState(runtime);

    ImGui::SeparatorText("Param Card (Editable)");

    bool show_debug_stats = panel_state.view.show_debug_stats;
    if (ImGui::Checkbox("Show Debug Stats", &show_debug_stats)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetShowDebugStats,
                                                 .bool_value = show_debug_stats});
        panel_state = BuildEditorPanelState(runtime);
    }

    bool manual_param_mode = panel_state.view.manual_param_mode;
    if (ImGui::Checkbox("Manual Param Mode", &manual_param_mode)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetManualParamMode,
                                                 .bool_value = manual_param_mode});
        panel_state = BuildEditorPanelState(runtime);
    }

    bool hair_spring_enabled = panel_state.view.hair_spring_enabled;
    if (ImGui::Checkbox("Hair Spring", &hair_spring_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetHairSpringEnabled,
                                                 .bool_value = hair_spring_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }

    bool simple_mask_enabled = panel_state.view.simple_mask_enabled;
    if (ImGui::Checkbox("Simple Mask", &simple_mask_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetSimpleMaskEnabled,
                                                 .bool_value = simple_mask_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }

    ImGui::SeparatorText("Head Pat Interaction");
    ImGui::Text("Head Hovering: %s", panel_state.view.head_pat_hovering ? "Yes" : "No");
    ImGui::Text("React TTL: %.3f s", panel_state.view.head_pat_react_ttl);
    ImGui::ProgressBar(panel_state.view.head_pat_progress, ImVec2(-1.0f, 0.0f), "Pat React");

    ImGui::SeparatorText("Feature Toggles");
    bool feature_scene_classifier_enabled = panel_state.view.feature_scene_classifier_enabled;
    if (ImGui::Checkbox("Enable Scene Classifier", &feature_scene_classifier_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureSceneClassifierEnabled,
                                                 .bool_value = feature_scene_classifier_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    bool feature_ocr_enabled = panel_state.view.feature_ocr_enabled;
    if (ImGui::Checkbox("Enable OCR", &feature_ocr_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureOcrEnabled,
                                                 .bool_value = feature_ocr_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    bool feature_face_emotion_enabled = panel_state.view.feature_face_emotion_enabled;
    if (ImGui::Checkbox("Enable Face Emotion", &feature_face_emotion_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureFaceEmotionEnabled,
                                                 .bool_value = feature_face_emotion_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    bool feature_asr_enabled = panel_state.view.feature_asr_enabled;
    if (ImGui::Checkbox("Enable ASR", &feature_asr_enabled)) {
        ApplyEditorPanelAction(runtime,
                               EditorPanelAction{.type = EditorPanelActionType::SetFeatureAsrEnabled,
                                                 .bool_value = feature_asr_enabled});
        panel_state = BuildEditorPanelState(runtime);
    }
    ImGui::TextDisabled("运行时 GUI 可通过 F1 或 FPS Overlay 快速开关；OCR 详细调参已收纳到“感知”页。");

    ImGui::SeparatorText("Param Panel Enhanced (Group/Search/Batch Bind)");
    if (!panel_state.view.model_loaded) {
        RenderUnifiedEmptyState("editor_no_model_empty_state",
                                "无模型",
                                "当前尚未加载模型，参数分组、批量绑定与编辑能力暂不可用。",
                                ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
    } else if (!panel_state.view.has_model_params) {
        RenderUnifiedEmptyState("editor_no_params_empty_state",
                                "无参数",
                                "当前模型没有可编辑参数，请先检查模型参数定义或导入结果。",
                                ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
    } else {
        const char *group_modes[] = {"Prefix", "Semantic"};
        int param_group_mode = panel_state.view.param_group_mode;
        if (ImGui::Combo("Param Group Mode", &param_group_mode, group_modes, 2)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetParamGroupMode,
                                                     .int_value = param_group_mode});
            panel_state = BuildEditorPanelState(runtime);
        }

        char param_search[128] = "";
        SDL_strlcpy(param_search, panel_state.view.param_search.c_str(), sizeof(param_search));
        if (ImGui::InputTextWithHint("Param Search", "fuzzy search by parameter name...", param_search,
                                     static_cast<int>(sizeof(param_search)))) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetParamSearch,
                                                     .text_value = param_search});
            panel_state = BuildEditorPanelState(runtime);
        }

        if (panel_state.view.has_param_groups) {
            const int selected_group_index = std::clamp(panel_state.view.selected_param_group_index,
                                                        0,
                                                        static_cast<int>(panel_state.view.group_options.size()) - 1);
            if (ImGui::BeginCombo("Param Group", panel_state.view.selected_group_label.c_str())) {
                for (int i = 0; i < static_cast<int>(panel_state.view.group_options.size()); ++i) {
                    const bool selected = i == selected_group_index;
                    if (ImGui::Selectable(panel_state.view.group_options[static_cast<std::size_t>(i)].label.c_str(), selected)) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::SelectParamGroup,
                                                                 .int_value = i});
                        panel_state = BuildEditorPanelState(runtime);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::Text("Group Param Count: %d", panel_state.view.selected_group_param_count);
            if (!panel_state.view.param_search.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("| filter: %s", panel_state.view.param_search.c_str());
            }
            if (!panel_state.view.selected_group_preview.empty()) {
                ImGui::TextWrapped("Preview: %s%s",
                                   panel_state.view.selected_group_preview.c_str(),
                                   panel_state.view.selected_group_param_count > 5 ? " ..." : "");
            }

            ImGui::SeparatorText("Selected Group Parameters");
            if (ImGui::BeginTable("selected_group_param_table",
                                  6,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Param", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthStretch, 0.12f);
                ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthStretch, 0.12f);
                ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.20f);
                ImGui::TableHeadersRow();

                for (const auto &row : panel_state.view.selected_group_param_rows) {
                    float target_value = row.target_value;
                    ImGui::PushID(row.param_index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(row.param_id.c_str(), row.selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::SelectParam,
                                                                 .int_value = row.param_index});
                        panel_state = BuildEditorPanelState(runtime);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f", row.min_value);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.3f", row.max_value);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.3f", row.default_value);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.3f", row.current_value);
                    ImGui::TableSetColumnIndex(5);
                    if (ImGui::SliderFloat("##target", &target_value, row.min_value, row.max_value, "%.3f")) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::SetParamTargetValue,
                                                                 .int_value = row.param_index,
                                                                 .float_value = target_value});
                        panel_state = BuildEditorPanelState(runtime);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            const char *binding_props[] = {"PosX", "PosY", "RotDeg", "ScaleX", "ScaleY", "Opacity"};
            int bind_prop_type = panel_state.view.batch_bind.bind_prop_type;
            if (ImGui::Combo("Bind Property", &bind_prop_type, binding_props, 6)) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetBatchBindPropType,
                                                         .int_value = bind_prop_type});
                panel_state = BuildEditorPanelState(runtime);
            }
            float bind_in_min = panel_state.view.batch_bind.bind_in_min;
            if (ImGui::SliderFloat("Bind In Min", &bind_in_min, -2.0f, 2.0f, "%.2f")) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetBatchBindInMin,
                                                         .float_value = bind_in_min});
                panel_state = BuildEditorPanelState(runtime);
            }
            float bind_in_max = panel_state.view.batch_bind.bind_in_max;
            if (ImGui::SliderFloat("Bind In Max", &bind_in_max, -2.0f, 2.0f, "%.2f")) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetBatchBindInMax,
                                                         .float_value = bind_in_max});
                panel_state = BuildEditorPanelState(runtime);
            }
            float bind_out_min = panel_state.view.batch_bind.bind_out_min;
            if (ImGui::SliderFloat("Bind Out Min", &bind_out_min, -180.0f, 180.0f, "%.2f")) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetBatchBindOutMin,
                                                         .float_value = bind_out_min});
                panel_state = BuildEditorPanelState(runtime);
            }
            float bind_out_max = panel_state.view.batch_bind.bind_out_max;
            if (ImGui::SliderFloat("Bind Out Max", &bind_out_max, -180.0f, 180.0f, "%.2f")) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetBatchBindOutMax,
                                                         .float_value = bind_out_max});
                panel_state = BuildEditorPanelState(runtime);
            }

            if (ImGui::Button("Apply Batch Bind -> Selected Part")) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::ApplyBatchBindToSelectedPart});
                panel_state = BuildEditorPanelState(runtime);
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply Batch Bind -> All Parts")) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::ApplyBatchBindToAllParts});
                panel_state = BuildEditorPanelState(runtime);
            }
        } else {
            RenderUnifiedEmptyState("editor_filtered_no_params_empty_state",
                                    "无参数",
                                    "当前搜索或分组条件下没有匹配参数，请调整关键字或切换分组方式。",
                                    ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
        }
    }
}


}  // namespace desktoper2D
