#include "app_debug_ui_panel_state.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui.h"

#include <algorithm>
#include <cctype>

#include "desktoper2D/lifecycle/ui/app_debug_ui_internal.h"

namespace desktoper2D {
    namespace {

        std::string ToLowerCopy(const std::string &input) {
            std::string out = input;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        bool ContainsCaseInsensitive(const std::string &text, const std::string &keyword) {
            if (keyword.empty()) {
                return false;
            }
            const std::string lower_text = ToLowerCopy(text);
            const std::string lower_keyword = ToLowerCopy(keyword);
            return lower_text.find(lower_keyword) != std::string::npos;
        }

    }  // namespace

    void RenderRuntimeEditorPanel(RuntimeUiView view) {
        AppRuntime &runtime = view.runtime;
        const EditorPanelState &panel_state = BuildEditorPanelState(runtime);

        ImGui::SeparatorText("Param Card (Editable)");

        bool show_debug_stats = panel_state.view.show_debug_stats;
        if (ImGui::Checkbox("Show Debug Stats", &show_debug_stats)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetShowDebugStats,
                                                     .bool_value = show_debug_stats});
            (void)BuildEditorPanelState(runtime);
        }

        bool manual_param_mode = panel_state.view.manual_param_mode;
        if (ImGui::Checkbox("Manual Param Mode", &manual_param_mode)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetManualParamMode,
                                                     .bool_value = manual_param_mode});
            (void)BuildEditorPanelState(runtime);
        }
        if (!panel_state.view.edit_runtime_hint.empty()) {
            ImGui::TextColored(panel_state.view.edit_mode ? ImVec4(1.0f, 0.75f, 0.35f, 1.0f)
                                                          : ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                               "%s",
                               panel_state.view.edit_runtime_hint.c_str());
        }

        bool hair_spring_enabled = panel_state.view.hair_spring_enabled;
        if (ImGui::Checkbox("Hair Spring", &hair_spring_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetHairSpringEnabled,
                                                     .bool_value = hair_spring_enabled});
            (void)BuildEditorPanelState(runtime);
        }

        bool simple_mask_enabled = panel_state.view.simple_mask_enabled;
        if (ImGui::Checkbox("Simple Mask", &simple_mask_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetSimpleMaskEnabled,
                                                     .bool_value = simple_mask_enabled});
            (void)BuildEditorPanelState(runtime);
        }

        ImGui::SeparatorText("Head Pat Interaction");
        ImGui::Text("Head Hovering: %s", panel_state.view.head_pat_hovering ? "Yes" : "No");
        ImGui::Text("React TTL: %.3f s", panel_state.view.head_pat_react_ttl);
        ImGui::ProgressBar(panel_state.view.head_pat_progress, ImVec2(-1.0f, 0.0f), "Pat React");

        ImGui::SeparatorText("Autosave");
        bool autosave_enabled = panel_state.view.autosave_enabled;
        if (ImGui::Checkbox("Enable Autosave", &autosave_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetAutosaveEnabled,
                                                     .bool_value = autosave_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        float autosave_interval = panel_state.view.autosave_interval_sec;
        if (ImGui::SliderFloat("Autosave Interval (sec)", &autosave_interval, 10.0f, 600.0f, "%.0f")) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetAutosaveIntervalSec,
                                                     .float_value = autosave_interval});
            (void)BuildEditorPanelState(runtime);
        }
        ImGui::Text("Next Autosave In: %.0f s", panel_state.view.autosave_remaining_sec);
        if (!panel_state.view.autosave_path.empty()) {
            ImGui::TextDisabled("Autosave Path: %s", panel_state.view.autosave_path.c_str());
        }
        if (!panel_state.view.autosave_last_error.empty()) {
            ImGui::TextColored(ImVec4(0.85f, 0.25f, 0.25f, 1.0f), "Autosave Error: %s", panel_state.view.autosave_last_error.c_str());
        }
        if (panel_state.view.autosave_recovery_available && !panel_state.view.autosave_recovery_prompted) {
            ImGui::OpenPopup("Autosave Recovery");
            runtime.editor_autosave_recovery_prompted = true;
        }
        if (ImGui::BeginPopupModal("Autosave Recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("妫€娴嬪埌鏈繚瀛樼殑鑷姩淇濆瓨鏂囦欢锛屾槸鍚︽仮澶嶇紪杈戜細璇濓紵");
            ImGui::Spacing();
            if (ImGui::Button("Recover Autosave")) {
                ApplyEditorPanelAction(runtime, EditorPanelAction{.type = EditorPanelActionType::RecoverFromAutosave});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard Autosave")) {
                ApplyEditorPanelAction(runtime, EditorPanelAction{.type = EditorPanelActionType::DiscardAutosave});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Later")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SeparatorText("Feature Toggles");
        bool feature_scene_classifier_enabled = panel_state.view.feature_scene_classifier_enabled;
        if (ImGui::Checkbox("Enable Scene Classifier", &feature_scene_classifier_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetFeatureSceneClassifierEnabled,
                                                     .bool_value = feature_scene_classifier_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        bool feature_ocr_enabled = panel_state.view.feature_ocr_enabled;
        if (ImGui::Checkbox("Enable OCR", &feature_ocr_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetFeatureOcrEnabled,
                                                     .bool_value = feature_ocr_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        bool feature_face_emotion_enabled = panel_state.view.feature_face_emotion_enabled;
        if (ImGui::Checkbox("Enable Face Emotion", &feature_face_emotion_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetFeatureFaceEmotionEnabled,
                                                     .bool_value = feature_face_emotion_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        bool feature_asr_enabled = panel_state.view.feature_asr_enabled;
        if (ImGui::Checkbox("Enable ASR", &feature_asr_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetFeatureAsrEnabled,
                                                     .bool_value = feature_asr_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        ImGui::TextDisabled("Runtime GUI can still be toggled with F1 or the FPS overlay. OCR tuning lives in the perception panels.");

        ImGui::SeparatorText("Pick Strategy");
        bool pick_lock_filter_enabled = panel_state.view.pick_lock_filter_enabled;
        if (ImGui::Checkbox("Enable Lock Filter", &pick_lock_filter_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetPickLockFilterEnabled,
                                                     .bool_value = pick_lock_filter_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        bool pick_scope_filter_enabled = panel_state.view.pick_scope_filter_enabled;
        if (ImGui::Checkbox("Enable Scope Filter", &pick_scope_filter_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetPickScopeFilterEnabled,
                                                     .bool_value = pick_scope_filter_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        if (panel_state.view.pick_scope_filter_enabled) {
            const char *scope_modes[] = {"All", "Selected", "Children"};
            int pick_scope_mode = panel_state.view.pick_scope_mode;
            if (ImGui::Combo("Scope Mode", &pick_scope_mode, scope_modes, 3)) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetPickScopeMode,
                                                         .int_value = pick_scope_mode});
                (void)BuildEditorPanelState(runtime);
            }
        }
        bool pick_name_filter_enabled = panel_state.view.pick_name_filter_enabled;
        if (ImGui::Checkbox("Enable Name Filter", &pick_name_filter_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetPickNameFilterEnabled,
                                                     .bool_value = pick_name_filter_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        if (panel_state.view.pick_name_filter_enabled) {
            char pick_name_filter[128] = "";
            SDL_strlcpy(pick_name_filter, panel_state.view.pick_name_filter.c_str(), sizeof(pick_name_filter));
            if (ImGui::InputTextWithHint("Pick Name", "id/texture contains...", pick_name_filter,
                                         static_cast<int>(sizeof(pick_name_filter)))) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::SetPickNameFilterText,
                                                         .text_value = pick_name_filter});
                (void)BuildEditorPanelState(runtime);
                                         }
        }
        bool pick_cycle_enabled = panel_state.view.pick_cycle_enabled;
        if (ImGui::Checkbox("Enable Cycle Pick", &pick_cycle_enabled)) {
            ApplyEditorPanelAction(runtime,
                                   EditorPanelAction{.type = EditorPanelActionType::SetPickCycleEnabled,
                                                     .bool_value = pick_cycle_enabled});
            (void)BuildEditorPanelState(runtime);
        }
        ImGui::BeginDisabled(!panel_state.view.pick_cycle_enabled);
        if (ImGui::Button("Cycle Next")) {
            runtime.pick_cycle_offset += 1;
            (void)BuildEditorPanelState(runtime);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Cycle")) {
            ApplyEditorPanelAction(runtime, EditorPanelAction{.type = EditorPanelActionType::ResetPickCycleOffset});
            (void)BuildEditorPanelState(runtime);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Cycle Offset: %d", panel_state.view.pick_cycle_offset);

        ImGui::SeparatorText("Param Panel Enhanced (Group/Search/Batch Bind)");
        if (!panel_state.view.model_loaded) {
            RenderUnifiedEmptyState("editor_no_model_empty_state",
                                    "No model loaded",
                                    "Load a model before using parameter grouping, batch bind, or editor controls.",
                                    ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
        } else if (!panel_state.view.has_model_params) {
            RenderUnifiedEmptyState("editor_no_params_empty_state",
                                    "No editable params",
                                    "The loaded model does not expose editable parameters.",
                                    ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
        } else {
            const bool panel_open = ImGui::CollapsingHeader(
                "Param Panel Overview",
                panel_state.view.param_panel_expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            if (ImGui::IsItemToggledOpen()) {
                ApplyEditorPanelAction(runtime,
                                       EditorPanelAction{.type = EditorPanelActionType::ToggleParamPanelExpanded,
                                                         .bool_value = panel_open});
                (void)BuildEditorPanelState(runtime);
            }

            if (panel_open) {
                const char *group_modes[] = {"Prefix", "Semantic"};
                int param_group_mode = panel_state.view.param_group_mode;
                if (ImGui::Combo("Param Group Mode", &param_group_mode, group_modes, 2)) {
                    ApplyEditorPanelAction(runtime,
                                           EditorPanelAction{.type = EditorPanelActionType::SetParamGroupMode,
                                                             .int_value = param_group_mode});
                    (void)BuildEditorPanelState(runtime);
                }

                char param_search[128] = "";
                SDL_strlcpy(param_search, panel_state.view.param_search.c_str(), sizeof(param_search));
                if (ImGui::InputTextWithHint("Param Search", "fuzzy search by parameter name...", param_search,
                                             static_cast<int>(sizeof(param_search)))) {
                    ApplyEditorPanelAction(runtime,
                                           EditorPanelAction{.type = EditorPanelActionType::SetParamSearch,
                                                             .text_value = param_search});
                    (void)BuildEditorPanelState(runtime);
                                             }
                if (!panel_state.view.param_search.empty()) {
                    ImGui::Text("Search Hits: %d / %d",
                                panel_state.view.param_search_hit_count,
                                static_cast<int>(runtime.model.parameters.size()));
                }

                const bool quick_open = ImGui::CollapsingHeader(
                    "Quick Params",
                    panel_state.view.param_quick_expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                if (ImGui::IsItemToggledOpen()) {
                    ApplyEditorPanelAction(runtime,
                                           EditorPanelAction{.type = EditorPanelActionType::ToggleParamQuickExpanded,
                                                             .bool_value = quick_open});
                    (void)BuildEditorPanelState(runtime);
                }
                if (quick_open) {
                    if (panel_state.view.quick_param_items.empty()) {
                        ImGui::TextDisabled("No quick params available.");
                    } else {
                        if (ImGui::Button("Group -> Default")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetGroupTargetsToDefault});
                            (void)BuildEditorPanelState(runtime);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Group -> Min")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetGroupTargetsToMin});
                            (void)BuildEditorPanelState(runtime);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Group -> Max")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetGroupTargetsToMax});
                            (void)BuildEditorPanelState(runtime);
                        }

                        if (ImGui::BeginTable("quick_param_table",
                                              2,
                                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                            ImGui::TableSetupColumn("Param", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                            ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                            ImGui::TableHeadersRow();
                            for (const auto &item : panel_state.view.quick_param_items) {
                                float target_value = item.target_value;
                                ImGui::PushID(item.param_index);
                                ImGui::TableNextRow();
                                if (item.search_hit && !panel_state.view.param_search.empty()) {
                                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(255, 225, 140, 70));
                                }
                                ImGui::TableSetColumnIndex(0);
                                if (ImGui::Selectable(item.label.c_str(), item.selected, ImGuiSelectableFlags_SpanAllColumns)) {
                                    ApplyEditorPanelAction(runtime,
                                                           EditorPanelAction{.type = EditorPanelActionType::SelectParam,
                                                                             .int_value = item.param_index});
                                    (void)BuildEditorPanelState(runtime);
                                }
                                ImGui::TableSetColumnIndex(1);
                                if (ImGui::SliderFloat("##quick_target",
                                                       &target_value,
                                                       item.min_value,
                                                       item.max_value,
                                                       "%.3f")) {
                                    ApplyEditorPanelAction(runtime,
                                                           EditorPanelAction{.type = EditorPanelActionType::SetParamTargetValue,
                                                                             .int_value = item.param_index,
                                                                             .float_value = target_value});
                                    (void)BuildEditorPanelState(runtime);
                                                       }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                                              }
                    }
                }

                if (panel_state.view.has_param_groups) {
                    const int selected_group_index = std::clamp(panel_state.view.selected_param_group_index,
                                                                0,
                                                                static_cast<int>(panel_state.view.group_options.size()) - 1);
                    if (ImGui::BeginCombo("Param Group", panel_state.view.selected_group_label.c_str())) {
                        for (int i = 0; i < static_cast<int>(panel_state.view.group_options.size()); ++i) {
                            const auto &option = panel_state.view.group_options[static_cast<std::size_t>(i)];
                            const bool selected = i == selected_group_index;
                            if (option.search_hit && !panel_state.view.param_search.empty()) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.35f, 1.0f));
                            }
                            if (ImGui::Selectable(option.label.c_str(), selected)) {
                                ApplyEditorPanelAction(runtime,
                                                       EditorPanelAction{.type = EditorPanelActionType::SelectParamGroup,
                                                                         .int_value = i});
                                (void)BuildEditorPanelState(runtime);
                            }
                            if (option.search_hit && !panel_state.view.param_search.empty()) {
                                ImGui::PopStyleColor();
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

                    const bool group_table_open = ImGui::CollapsingHeader(
                        "Selected Group Parameters",
                        panel_state.view.param_group_table_expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    if (ImGui::IsItemToggledOpen()) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::ToggleParamGroupTableExpanded,
                                                                 .bool_value = group_table_open});
                        (void)BuildEditorPanelState(runtime);
                    }
                    if (group_table_open) {
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
                                if (row.search_hit && !panel_state.view.param_search.empty()) {
                                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(255, 225, 140, 70));
                                }
                                ImGui::TableSetColumnIndex(0);
                                if (ImGui::Selectable(row.param_id.c_str(), row.selected, ImGuiSelectableFlags_SpanAllColumns)) {
                                    ApplyEditorPanelAction(runtime,
                                                           EditorPanelAction{.type = EditorPanelActionType::SelectParam,
                                                                             .int_value = row.param_index});
                                    (void)BuildEditorPanelState(runtime);
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
                                    (void)BuildEditorPanelState(runtime);
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                                              }
                    }

                    const bool batch_bind_open = ImGui::CollapsingHeader(
                        "Batch Bind",
                        panel_state.view.param_batch_bind_expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                    if (ImGui::IsItemToggledOpen()) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::ToggleParamBatchBindExpanded,
                                                                 .bool_value = batch_bind_open});
                        (void)BuildEditorPanelState(runtime);
                    }
                    if (batch_bind_open) {
                        const auto &batch_bind = panel_state.view.batch_bind;
                        const auto &templates = batch_bind.templates;
                        int bind_template_index = batch_bind.bind_template_index;
                        const char *current_template_label = "Custom";
                        if (bind_template_index > 0 && bind_template_index <= static_cast<int>(templates.size())) {
                            current_template_label = templates[static_cast<std::size_t>(bind_template_index - 1)].label.c_str();
                        }
                        if (ImGui::BeginCombo("Bind Template", current_template_label)) {
                            const bool is_custom = bind_template_index == 0;
                            if (ImGui::Selectable("Custom", is_custom)) {
                                ApplyEditorPanelAction(runtime,
                                                       EditorPanelAction{.type = EditorPanelActionType::SetBatchBindTemplateIndex,
                                                                         .int_value = 0});
                                (void)BuildEditorPanelState(runtime);
                            }
                            for (int i = 0; i < static_cast<int>(templates.size()); ++i) {
                                const bool selected = bind_template_index == (i + 1);
                                if (ImGui::Selectable(templates[static_cast<std::size_t>(i)].label.c_str(), selected)) {
                                    ApplyEditorPanelAction(runtime,
                                                           EditorPanelAction{.type = EditorPanelActionType::SetBatchBindTemplateIndex,
                                                                             .int_value = i + 1});
                                    (void)BuildEditorPanelState(runtime);
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        const char *binding_props[] = {"PosX", "PosY", "RotDeg", "ScaleX", "ScaleY", "Opacity"};
                        int bind_prop_type = panel_state.view.batch_bind.bind_prop_type;
                        if (ImGui::Combo("Bind Property", &bind_prop_type, binding_props, 6)) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetBatchBindPropType,
                                                                     .int_value = bind_prop_type});
                            (void)BuildEditorPanelState(runtime);
                        }
                        float bind_in_min = panel_state.view.batch_bind.bind_in_min;
                        if (ImGui::SliderFloat("Bind In Min", &bind_in_min, -2.0f, 2.0f, "%.2f")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetBatchBindInMin,
                                                                     .float_value = bind_in_min});
                            (void)BuildEditorPanelState(runtime);
                        }
                        float bind_in_max = panel_state.view.batch_bind.bind_in_max;
                        if (ImGui::SliderFloat("Bind In Max", &bind_in_max, -2.0f, 2.0f, "%.2f")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetBatchBindInMax,
                                                                     .float_value = bind_in_max});
                            (void)BuildEditorPanelState(runtime);
                        }
                        float bind_out_min = panel_state.view.batch_bind.bind_out_min;
                        if (ImGui::SliderFloat("Bind Out Min", &bind_out_min, -180.0f, 180.0f, "%.2f")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetBatchBindOutMin,
                                                                     .float_value = bind_out_min});
                            (void)BuildEditorPanelState(runtime);
                        }
                        float bind_out_max = panel_state.view.batch_bind.bind_out_max;
                        if (ImGui::SliderFloat("Bind Out Max", &bind_out_max, -180.0f, 180.0f, "%.2f")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::SetBatchBindOutMax,
                                                                     .float_value = bind_out_max});
                            (void)BuildEditorPanelState(runtime);
                        }

                        if (!panel_state.view.batch_bind.validation.valid) {
                            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.3f, 1.0f),
                                               "Bind Validation: %s",
                                               panel_state.view.batch_bind.validation.message.c_str());
                        } else {
                            ImGui::TextDisabled("Bind Validation: OK");
                        }

                        ImGui::BeginDisabled(!panel_state.view.batch_bind.validation.valid ||
                                             !panel_state.view.batch_bind.can_apply_to_selected_part);
                        if (ImGui::Button("Apply Batch Bind -> Selected Part")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::ApplyBatchBindToSelectedPart});
                            (void)BuildEditorPanelState(runtime);
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        ImGui::BeginDisabled(!panel_state.view.batch_bind.validation.valid ||
                                             !panel_state.view.batch_bind.can_apply_to_all_parts);
                        if (ImGui::Button("Apply Batch Bind -> All Parts")) {
                            ApplyEditorPanelAction(runtime,
                                                   EditorPanelAction{.type = EditorPanelActionType::ApplyBatchBindToAllParts});
                            (void)BuildEditorPanelState(runtime);
                        }
                        ImGui::EndDisabled();
                    }
                } else {
                    RenderUnifiedEmptyState("editor_filtered_no_params_empty_state",
                                            "No matching params",
                                            "No parameters match the current search or grouping filters.",
                                            ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
                }
            }

            ImGui::SeparatorText("Command History");
            if (panel_state.view.history_entries.empty()) {
                RenderUnifiedEmptyState("editor_history_empty_state",
                                        "No history yet",
                                        "Make an edit to populate command history and diff preview.",
                                        ImVec4(0.70f, 0.78f, 1.0f, 1.0f));
                return;
            }

            ImGui::BeginChild("editor_history_child", ImVec2(-1.0f, 240.0f), ImGuiChildFlags_Borders);
            if (ImGui::BeginTable("editor_history_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 0.12f);
                ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch, 0.48f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                ImGui::TableHeadersRow();

                const int max_index = static_cast<int>(panel_state.view.history_entries.size()) - 1;
                for (const auto &entry : panel_state.view.history_entries) {
                    ImGui::PushID(entry.index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(entry.applied ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                                                     : ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
                                       "%s",
                                       entry.applied ? "Applied" : "Pending");
                    ImGui::TableSetColumnIndex(1);
                    const bool selected = panel_state.view.history_selected_index == entry.index;
                    if (ImGui::Selectable(entry.label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::SelectHistoryIndex,
                                                                 .int_value = entry.index,
                                                                 .int_value2 = max_index});
                        (void)BuildEditorPanelState(runtime);
                    }
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::Button("Jump")) {
                        ApplyEditorPanelAction(runtime,
                                               EditorPanelAction{.type = EditorPanelActionType::JumpToHistoryIndex,
                                                                 .int_value = entry.target_undo_size});
                        (void)BuildEditorPanelState(runtime);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndChild();

            ImGui::SeparatorText("Command Diff Preview");
            std::string history_detail = panel_state.view.history_detail;
            if (history_detail.empty()) {
                history_detail = "Select a history entry to inspect its diff.";
            }
            RenderLongTextBlock("History Detail", "editor_history_detail", &history_detail, 10, 140.0f);
        }


    }  // namespace desktoper2D
}

