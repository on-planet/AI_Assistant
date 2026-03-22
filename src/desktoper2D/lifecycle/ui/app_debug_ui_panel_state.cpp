#include "desktoper2D/lifecycle/ui/app_debug_ui_panel_state.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "app_debug_ui_internal.h"
#include "desktoper2D/lifecycle/editor/editor_session_service.h"
#include "desktoper2D/lifecycle/ui/app_debug_ui_editor_service.h"

namespace desktoper2D {

TimelinePanelState BuildTimelinePanelState(const AppRuntime &runtime) {
    TimelinePanelState state{};
    state.view.has_model_params = !runtime.model.parameters.empty();
    state.view.has_channels = !runtime.model.animation_channels.empty();
    state.view.selected_param_index = runtime.selected_param_index;
    state.view.selected_channel_index = runtime.timeline_selected_channel_index;
    state.view.selected_keyframe_count = static_cast<int>(runtime.timeline_selected_keyframe_indices.size());
    state.view.clipboard_keyframe_count = static_cast<int>(runtime.timeline_keyframe_clipboard.size());
    state.view.undo_count = static_cast<int>(runtime.undo_stack.size());
    state.view.redo_count = static_cast<int>(runtime.redo_stack.size());
    state.view.cursor_sec = runtime.timeline_cursor_sec;
    state.view.duration_sec = runtime.timeline_duration_sec;
    state.view.snap_fps = runtime.timeline_snap_fps;
    state.view.snap_enabled = runtime.timeline_snap_enabled;
    state.view.snap_mode = runtime.timeline_snap_mode;
    state.view.can_add_channel = state.view.has_model_params;
    state.view.can_delete_channel = state.view.has_channels;
    state.view.can_add_or_update_keyframe = state.view.has_channels && state.view.has_model_params;
    state.view.can_copy_selected_keyframes = state.view.has_channels && state.view.selected_keyframe_count > 0;
    state.view.can_paste = state.view.has_channels && state.view.clipboard_keyframe_count > 0;
    state.view.can_remove_last_keyframe = false;
    state.view.can_undo = !runtime.undo_stack.empty();
    state.view.can_redo = !runtime.redo_stack.empty();
    const auto &interaction = GetTimelineInteractionStorage();
    state.view.drag_snapshot_captured = interaction.drag_snapshot_captured;
    state.view.dragging_keyframe_index = interaction.dragging_keyframe_index;
    state.view.dragging_channel_index = interaction.dragging_channel_index;
    state.view.box_select_active = interaction.box_select_active;
    state.view.box_select_start_x = interaction.box_select_start_x;
    state.view.box_select_start_y = interaction.box_select_start_y;
    state.view.box_select_end_x = interaction.box_select_end_x;
    state.view.box_select_end_y = interaction.box_select_end_y;
    state.view.selected_keyframe_indices = runtime.timeline_selected_keyframe_indices;
    state.view.selected_keyframe_ids = runtime.timeline_selected_keyframe_ids;

    state.form.timeline_enabled = runtime.timeline_enabled;
    state.form.cursor_sec = runtime.timeline_cursor_sec;
    state.form.duration_sec = runtime.timeline_duration_sec;
    state.form.snap_enabled = runtime.timeline_snap_enabled;
    state.form.snap_mode = runtime.timeline_snap_mode;
    state.form.snap_fps = runtime.timeline_snap_fps;
    state.form.selected_channel_index = runtime.timeline_selected_channel_index;

    state.view.param_options.reserve(runtime.model.parameters.size());
    for (std::size_t i = 0; i < runtime.model.parameters.size(); ++i) {
        const auto &param = runtime.model.parameters[i];
        state.view.param_options.push_back(TimelineOptionItem{.label = param.id,
                                                             .value = static_cast<int>(i)});
    }

    state.view.channel_options.reserve(runtime.model.animation_channels.size());
    for (std::size_t i = 0; i < runtime.model.animation_channels.size(); ++i) {
        const auto &channel = runtime.model.animation_channels[i];
        state.view.channel_options.push_back(TimelineOptionItem{.label = channel.id,
                                                               .value = static_cast<int>(i)});
    }

    if (!runtime.model.animation_channels.empty()) {
        const int ch_idx = std::clamp(runtime.timeline_selected_channel_index,
                                      0,
                                      static_cast<int>(runtime.model.animation_channels.size()) - 1);
        const auto &ch = runtime.model.animation_channels[static_cast<std::size_t>(ch_idx)];
        state.view.active_channel.id = ch.id;
        state.view.active_channel.param_index = ch.param_index;
        state.view.active_channel.enabled = ch.enabled;
        state.view.active_channel.interpolation = ch.timeline_interp;
        state.view.active_channel.wrap = ch.timeline_wrap;
        state.view.active_channel.keyframes = ch.keyframes;
        state.view.can_remove_last_keyframe = !ch.keyframes.empty();

        if (!runtime.model.parameters.empty()) {
            const int param_idx = std::clamp(ch.param_index, 0, static_cast<int>(runtime.model.parameters.size()) - 1);
            const auto &spec = runtime.model.parameters[static_cast<std::size_t>(param_idx)].param.spec();
            state.view.active_param_min_value = std::min(spec.min_value, spec.max_value);
            state.view.active_param_max_value = std::max(spec.min_value, spec.max_value);
            if (std::abs(state.view.active_param_max_value - state.view.active_param_min_value) < 1e-6f) {
                state.view.active_param_max_value = state.view.active_param_min_value + 1.0f;
            }
        }
    }

    return state;
}

std::vector<EditorBatchBindTemplateItem> BuildEditorBatchBindTemplates() {
    std::vector<EditorBatchBindTemplateItem> items;
    items.push_back(EditorBatchBindTemplateItem{.label = "Move X (-1..1 -> -80..80)",
                                                .bind_prop_type = BindingType::PosX,
                                                .bind_in_min = -1.0f,
                                                .bind_in_max = 1.0f,
                                                .bind_out_min = -80.0f,
                                                .bind_out_max = 80.0f});
    items.push_back(EditorBatchBindTemplateItem{.label = "Move Y (-1..1 -> -80..80)",
                                                .bind_prop_type = BindingType::PosY,
                                                .bind_in_min = -1.0f,
                                                .bind_in_max = 1.0f,
                                                .bind_out_min = -80.0f,
                                                .bind_out_max = 80.0f});
    items.push_back(EditorBatchBindTemplateItem{.label = "Rotate Small (-1..1 -> -15..15)",
                                                .bind_prop_type = BindingType::RotDeg,
                                                .bind_in_min = -1.0f,
                                                .bind_in_max = 1.0f,
                                                .bind_out_min = -15.0f,
                                                .bind_out_max = 15.0f});
    items.push_back(EditorBatchBindTemplateItem{.label = "Rotate Wide (-1..1 -> -45..45)",
                                                .bind_prop_type = BindingType::RotDeg,
                                                .bind_in_min = -1.0f,
                                                .bind_in_max = 1.0f,
                                                .bind_out_min = -45.0f,
                                                .bind_out_max = 45.0f});
    items.push_back(EditorBatchBindTemplateItem{.label = "Opacity (0..1 -> 0..1)",
                                                .bind_prop_type = BindingType::Opacity,
                                                .bind_in_min = 0.0f,
                                                .bind_in_max = 1.0f,
                                                .bind_out_min = 0.0f,
                                                .bind_out_max = 1.0f});
    items.push_back(EditorBatchBindTemplateItem{.label = "Scale (0..1 -> 0.9..1.1)",
                                                .bind_prop_type = BindingType::ScaleX,
                                                .bind_in_min = 0.0f,
                                                .bind_in_max = 1.0f,
                                                .bind_out_min = 0.9f,
                                                .bind_out_max = 1.1f});
    return items;
}

std::pair<float, float> BatchBindOutRangeFor(BindingType type) {
    switch (type) {
        case BindingType::PosX:
        case BindingType::PosY:
            return {-500.0f, 500.0f};
        case BindingType::RotDeg:
            return {-180.0f, 180.0f};
        case BindingType::ScaleX:
        case BindingType::ScaleY:
            return {0.0f, 3.0f};
        case BindingType::Opacity:
            return {0.0f, 1.0f};
        default:
            return {-180.0f, 180.0f};
    }
}

EditorBatchBindValidation ValidateBatchBind(const std::vector<EditorParamRowState> &param_rows,
                                            BindingType type,
                                            float in_min,
                                            float in_max,
                                            float out_min,
                                            float out_max) {
    EditorBatchBindValidation v{};
    v.in_min_max_ok = in_min <= in_max;
    v.out_min_max_ok = out_min <= out_max;

    v.in_range_ok = true;
    for (const auto &row : param_rows) {
        if (in_min < std::min(row.min_value, row.max_value) || in_max > std::max(row.min_value, row.max_value)) {
            v.in_range_ok = false;
            break;
        }
    }

    const auto range = BatchBindOutRangeFor(type);
    v.out_range_ok = out_min >= range.first && out_max <= range.second;

    v.valid = v.in_min_max_ok && v.out_min_max_ok && v.in_range_ok && v.out_range_ok;
    if (!v.valid) {
        std::string msg;
        if (!v.in_min_max_ok) msg += "输入范围最小值大于最大值。";
        if (!v.out_min_max_ok) msg += (msg.empty() ? "" : " ") + std::string("输出范围最小值大于最大值。");
        if (!v.in_range_ok) msg += (msg.empty() ? "" : " ") + std::string("输入范围超出参数规格范围。");
        if (!v.out_range_ok) msg += (msg.empty() ? "" : " ") + std::string("输出范围超出属性安全范围。");
        v.message = msg.empty() ? "绑定范围非法。" : msg;
    }
    return v;
}

namespace {

const char *EditCommandTypeLabel(EditCommand::Type type) {
    switch (type) {
        case EditCommand::Type::Transform: return "Transform";
        case EditCommand::Type::Timeline: return "Timeline";
        case EditCommand::Type::Param: return "Param";
        case EditCommand::Type::Inspector: return "Inspector";
        case EditCommand::Type::Reminder: return "Reminder";
        default: return "Unknown";
    }
}

std::string FormatFloatDelta(float before_value, float after_value, const char *suffix = nullptr) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss << std::setprecision(3);
    const float delta = after_value - before_value;
    oss << before_value << " -> " << after_value << " (";
    if (delta >= 0.0f) {
        oss << "+";
    }
    oss << delta;
    oss << ")";
    if (suffix && suffix[0] != '\0') {
        oss << suffix;
    }
    return oss.str();
}

std::string FormatBoolDelta(bool before_value, bool after_value) {
    return std::string(before_value ? "true" : "false") + " -> " + (after_value ? "true" : "false");
}

std::string FormatReminderSummary(const ReminderItem &item) {
    std::ostringstream oss;
    if (!item.title.empty()) {
        oss << item.title;
    } else {
        oss << "(untitled)";
    }
    if (item.due_unix_sec > 0) {
        oss << " @" << item.due_unix_sec;
    }
    return oss.str();
}

std::string BuildEditCommandSummary(const EditCommand &cmd) {
    std::string label = EditCommandTypeLabel(cmd.type);
    switch (cmd.type) {
        case EditCommand::Type::Transform: {
            const std::string part = cmd.part_id.empty() ? "(part)" : cmd.part_id;
            return label + ": " + part;
        }
        case EditCommand::Type::Timeline: {
            const std::string ch = cmd.channel_id.empty() ? "(channel)" : cmd.channel_id;
            return label + ": " + ch;
        }
        case EditCommand::Type::Param: {
            const std::string pid = cmd.param_id.empty() ? "(param)" : cmd.param_id;
            return label + ": " + pid;
        }
        case EditCommand::Type::Inspector: {
            const std::string part = cmd.part_id.empty() ? "(part)" : cmd.part_id;
            return label + ": " + part;
        }
        case EditCommand::Type::Reminder: {
            std::ostringstream oss;
            oss << label;
            if (cmd.reminder_id != 0) {
                oss << " #" << cmd.reminder_id;
            }
            return oss.str();
        }
        default:
            return label;
    }
}

std::string BuildEditCommandDetail(const EditCommand &cmd) {
    std::ostringstream oss;
    oss << EditCommandTypeLabel(cmd.type);
    switch (cmd.type) {
        case EditCommand::Type::Transform: {
            oss << "\npart: " << (cmd.part_id.empty() ? "(unknown)" : cmd.part_id);
            oss << "\npos: " << FormatFloatDelta(cmd.before_pos_x, cmd.after_pos_x, ", ")
                << FormatFloatDelta(cmd.before_pos_y, cmd.after_pos_y);
            oss << "\npivot: " << FormatFloatDelta(cmd.before_pivot_x, cmd.after_pivot_x, ", ")
                << FormatFloatDelta(cmd.before_pivot_y, cmd.after_pivot_y);
            oss << "\nrot: " << FormatFloatDelta(cmd.before_rot_deg, cmd.after_rot_deg, " deg");
            oss << "\nscale: " << FormatFloatDelta(cmd.before_scale_x, cmd.after_scale_x, ", ")
                << FormatFloatDelta(cmd.before_scale_y, cmd.after_scale_y);
            break;
        }
        case EditCommand::Type::Timeline: {
            oss << "\nchannel: " << (cmd.channel_id.empty() ? "(unknown)" : cmd.channel_id);
            oss << "\nkeyframes: " << cmd.before_keyframes.size() << " -> " << cmd.after_keyframes.size();
            oss << "\nselected: " << cmd.before_selected_keyframe_ids.size()
                << " -> " << cmd.after_selected_keyframe_ids.size();
            break;
        }
        case EditCommand::Type::Param: {
            oss << "\nparam: " << (cmd.param_id.empty() ? "(unknown)" : cmd.param_id);
            oss << "\nvalue: " << FormatFloatDelta(cmd.before_param_value, cmd.after_param_value);
            break;
        }
        case EditCommand::Type::Inspector: {
            oss << "\npart: " << (cmd.part_id.empty() ? "(unknown)" : cmd.part_id);
            oss << "\nopacity: " << FormatFloatDelta(cmd.before_opacity, cmd.after_opacity);
            oss << "\ndeformer: " << cmd.before_deformer_type << " -> " << cmd.after_deformer_type;
            oss << "\nrot weight: " << FormatFloatDelta(cmd.before_rotation_weight, cmd.after_rotation_weight);
            oss << "\nrot speed: " << FormatFloatDelta(cmd.before_rotation_speed, cmd.after_rotation_speed);
            oss << "\nwarp weight: " << FormatFloatDelta(cmd.before_warp_weight, cmd.after_warp_weight);
            break;
        }
        case EditCommand::Type::Reminder: {
            const std::string before_summary = cmd.reminder_had_before ? FormatReminderSummary(cmd.before_reminder) : "(none)";
            const std::string after_summary = cmd.reminder_had_after ? FormatReminderSummary(cmd.after_reminder) : "(none)";
            oss << "\nreminder: " << before_summary << " -> " << after_summary;
            oss << "\ncompleted: " << FormatBoolDelta(cmd.before_reminder.completed, cmd.after_reminder.completed);
            break;
        }
        default:
            break;
    }
    return oss.str();
}

}  // namespace

EditorPanelState BuildEditorPanelState(const AppRuntime &runtime) {
    EditorPanelState state{};
    state.view.model_loaded = runtime.model_loaded;
    state.view.has_model_params = runtime.model_loaded && !runtime.model.parameters.empty();
    state.view.show_debug_stats = runtime.show_debug_stats;
    state.view.manual_param_mode = runtime.manual_param_mode;
    state.view.edit_mode = runtime.edit_mode;
    if (runtime.edit_mode && runtime.manual_param_mode) {
        state.view.edit_runtime_hint = "编辑态 + 手动参数：将覆盖运行态/动画通道参数";
    } else if (runtime.manual_param_mode && !runtime.edit_mode) {
        state.view.edit_runtime_hint = "手动参数开启：运行态参数将被手动输入覆盖";
    } else if (runtime.edit_mode && !runtime.manual_param_mode) {
        state.view.edit_runtime_hint = "编辑态：建议开启手动参数后再微调参数";
    } else {
        state.view.edit_runtime_hint = "运行态：参数由动画/行为驱动";
    }
    state.view.hair_spring_enabled = runtime.model.enable_hair_spring;
    state.view.simple_mask_enabled = runtime.model.enable_simple_mask;
    state.view.head_pat_hovering = runtime.interaction_state.head_pat_hovering;
    state.view.head_pat_react_ttl = runtime.interaction_state.head_pat_react_ttl;
    state.view.head_pat_progress = std::clamp(runtime.interaction_state.head_pat_react_ttl / 0.35f, 0.0f, 1.0f);
    state.view.feature_scene_classifier_enabled = runtime.feature_flags.scene_classifier_enabled;
    state.view.feature_ocr_enabled = runtime.feature_flags.ocr_enabled;
    state.view.feature_face_emotion_enabled = runtime.feature_flags.face_emotion_enabled;
    state.view.feature_asr_enabled = runtime.feature_flags.asr_enabled;
    state.view.feature_plugin_enabled = runtime.feature_flags.plugin_enabled;
    state.view.pick_lock_filter_enabled = runtime.pick_lock_filter_enabled;
    state.view.pick_scope_filter_enabled = runtime.pick_scope_filter_enabled;
    state.view.pick_scope_mode = runtime.pick_scope_mode;
    state.view.pick_name_filter_enabled = runtime.pick_name_filter_enabled;
    state.view.pick_name_filter = runtime.pick_name_filter;
    state.view.pick_cycle_enabled = runtime.pick_cycle_enabled;
    state.view.pick_cycle_offset = runtime.pick_cycle_offset;
    state.view.axis_constraint = runtime.axis_constraint;
    state.view.snap_enabled = runtime.snap_enabled;
    state.view.snap_grid = runtime.snap_grid;
    state.view.drag_sensitivity = runtime.editor_drag_sensitivity;
    state.view.gizmo_sensitivity = runtime.editor_gizmo_sensitivity;
    state.view.autosave_enabled = runtime.editor_autosave_enabled;
    state.view.autosave_interval_sec = runtime.editor_autosave_interval_sec;
    state.view.autosave_remaining_sec = std::max(0.0f, runtime.editor_autosave_interval_sec - runtime.editor_autosave_accum_sec);
    state.view.autosave_recovery_available = runtime.editor_autosave_recovery_available;
    state.view.autosave_recovery_prompted = runtime.editor_autosave_recovery_prompted;
    state.view.autosave_recovery_checked = runtime.editor_autosave_recovery_checked;
    state.view.autosave_path = runtime.editor_autosave_path;
    state.view.autosave_last_error = runtime.editor_autosave_last_error;
    state.view.param_group_mode = runtime.param_group_mode;
    state.view.param_search = runtime.param_search;
    state.view.param_panel_expanded = runtime.editor_param_panel_expanded;
    state.view.param_quick_expanded = runtime.editor_param_quick_expanded;
    state.view.param_group_table_expanded = runtime.editor_param_group_table_expanded;
    state.view.param_batch_bind_expanded = runtime.editor_param_batch_bind_expanded;

    if (!state.view.has_model_params) {
        return state;
    }

    state.view.param_search_hit_count = 0;
    for (const auto &param : runtime.model.parameters) {
        if (ParamMatchesSearch(param.id, runtime.param_search)) {
            state.view.param_search_hit_count += 1;
        }
    }

    const std::vector<ParamGroup> groups = BuildParamGroups(runtime, runtime.param_group_mode, runtime.param_search);
    state.view.has_param_groups = !groups.empty();
    if (!state.view.has_param_groups) {
        return state;
    }

    state.view.group_options.reserve(groups.size());
    for (const auto &group : groups) {
        EditorParamGroupOption option{};
        option.label = group.first + " (" + std::to_string(group.second.size()) + ")";
        option.param_indices = group.second;

        const int max_preview = std::min(5, static_cast<int>(group.second.size()));
        for (int i = 0; i < max_preview; ++i) {
            const int idx = group.second[static_cast<std::size_t>(i)];
            if (idx < 0 || idx >= static_cast<int>(runtime.model.parameters.size())) {
                continue;
            }
            if (!option.preview.empty()) {
                option.preview += ", ";
            }
            option.preview += runtime.model.parameters[static_cast<std::size_t>(idx)].id;
        }
        option.search_hit = false;
        if (runtime.param_search[0] != '\0') {
            for (int idx : group.second) {
                if (idx < 0 || idx >= static_cast<int>(runtime.model.parameters.size())) {
                    continue;
                }
                if (ParamMatchesSearch(runtime.model.parameters[static_cast<std::size_t>(idx)].id, runtime.param_search)) {
                    option.search_hit = true;
                    break;
                }
            }
        }
        state.view.group_options.push_back(std::move(option));
    }

    state.view.selected_param_group_index = std::clamp(runtime.selected_param_group_index,
                                                       0,
                                                       static_cast<int>(state.view.group_options.size()) - 1);
    const auto &selected_group = state.view.group_options[static_cast<std::size_t>(state.view.selected_param_group_index)];
    state.view.selected_group_label = selected_group.label;
    state.view.selected_group_preview = selected_group.preview;
    state.view.selected_group_param_indices = selected_group.param_indices;
    state.view.selected_group_param_count = static_cast<int>(selected_group.param_indices.size());

    state.view.selected_group_param_rows.reserve(selected_group.param_indices.size());
    for (int param_idx : selected_group.param_indices) {
        if (param_idx < 0 || param_idx >= static_cast<int>(runtime.model.parameters.size())) {
            continue;
        }
        const auto &model_param = runtime.model.parameters[static_cast<std::size_t>(param_idx)];
        const auto &param = model_param.param;
        const auto &spec = param.spec();
        state.view.selected_group_param_rows.push_back(EditorParamRowState{
            .param_index = param_idx,
            .param_id = model_param.id,
            .min_value = spec.min_value,
            .max_value = spec.max_value,
            .default_value = spec.default_value,
            .current_value = param.value(),
            .target_value = param.target(),
            .selected = runtime.selected_param_index == param_idx,
            .search_hit = ParamMatchesSearch(model_param.id, runtime.param_search),
        });
    }

    state.view.quick_param_items.clear();
    state.view.quick_param_items.reserve(6);
    if (runtime.selected_param_index >= 0 &&
        runtime.selected_param_index < static_cast<int>(runtime.model.parameters.size())) {
        const auto &model_param = runtime.model.parameters[static_cast<std::size_t>(runtime.selected_param_index)];
        const auto &param = model_param.param;
        const auto &spec = param.spec();
        state.view.quick_param_items.push_back(EditorQuickParamItem{
            .param_index = runtime.selected_param_index,
            .label = model_param.id,
            .min_value = spec.min_value,
            .max_value = spec.max_value,
            .target_value = param.target(),
            .selected = true,
            .search_hit = ParamMatchesSearch(model_param.id, runtime.param_search),
        });
    }
    for (const auto &row : state.view.selected_group_param_rows) {
        if (static_cast<int>(state.view.quick_param_items.size()) >= 6) {
            break;
        }
        if (row.param_index == runtime.selected_param_index) {
            continue;
        }
        state.view.quick_param_items.push_back(EditorQuickParamItem{
            .param_index = row.param_index,
            .label = row.param_id,
            .min_value = row.min_value,
            .max_value = row.max_value,
            .target_value = row.target_value,
            .selected = row.selected,
            .search_hit = row.search_hit,
        });
    }

    state.view.batch_bind.bind_prop_type = std::clamp(runtime.batch_bind_prop_type, 0, 5);
    state.view.batch_bind.bind_template_index = std::max(0, runtime.batch_bind_template_index);
    state.view.batch_bind.bind_in_min = runtime.batch_bind_in_min;
    state.view.batch_bind.bind_in_max = runtime.batch_bind_in_max;
    state.view.batch_bind.bind_out_min = runtime.batch_bind_out_min;
    state.view.batch_bind.bind_out_max = runtime.batch_bind_out_max;
    state.view.batch_bind.templates = BuildEditorBatchBindTemplates();
    state.view.batch_bind.validation = ValidateBatchBind(state.view.selected_group_param_rows,
                                                        static_cast<BindingType>(state.view.batch_bind.bind_prop_type),
                                                        state.view.batch_bind.bind_in_min,
                                                        state.view.batch_bind.bind_in_max,
                                                        state.view.batch_bind.bind_out_min,
                                                        state.view.batch_bind.bind_out_max);
    state.view.batch_bind.can_apply_to_selected_part = runtime.selected_part_index >= 0 &&
                                                       runtime.selected_part_index < static_cast<int>(runtime.model.parts.size()) &&
                                                       !selected_group.param_indices.empty();
    state.view.batch_bind.can_apply_to_all_parts = !runtime.model.parts.empty() && !selected_group.param_indices.empty();
    if (state.view.batch_bind.can_apply_to_selected_part) {
        state.view.batch_bind.selected_part_label = runtime.model.parts[static_cast<std::size_t>(runtime.selected_part_index)].id;
    }

    const int undo_count = static_cast<int>(runtime.undo_stack.size());
    const int redo_count = static_cast<int>(runtime.redo_stack.size());
    int list_index = 0;
    state.view.history_entries.reserve(runtime.undo_stack.size() + runtime.redo_stack.size());
    for (int i = undo_count - 1; i >= 0; --i) {
        const auto &cmd = runtime.undo_stack[static_cast<std::size_t>(i)];
        EditorHistoryEntry entry{};
        entry.index = list_index++;
        entry.target_undo_size = i;
        entry.label = BuildEditCommandSummary(cmd);
        entry.detail = BuildEditCommandDetail(cmd);
        entry.applied = true;
        state.view.history_entries.push_back(std::move(entry));
    }

    for (int i = redo_count - 1; i >= 0; --i) {
        const auto &cmd = runtime.redo_stack[static_cast<std::size_t>(i)];
        EditorHistoryEntry entry{};
        entry.index = list_index++;
        entry.target_undo_size = undo_count + (redo_count - i);
        entry.label = BuildEditCommandSummary(cmd);
        entry.detail = BuildEditCommandDetail(cmd);
        entry.applied = false;
        state.view.history_entries.push_back(std::move(entry));
    }

    if (!state.view.history_entries.empty()) {
        const int max_index = static_cast<int>(state.view.history_entries.size()) - 1;
        state.view.history_selected_index = std::clamp(runtime.editor_history_selected_index, 0, max_index);
        state.view.history_detail = state.view.history_entries[static_cast<std::size_t>(state.view.history_selected_index)].detail;
    } else {
        state.view.history_selected_index = -1;
        state.view.history_detail.clear();
    }
    return state;
}

void RestoreTimelineSelectionFromHistory(AppRuntime &runtime, const std::vector<std::uint64_t> &selected_ids) {
    runtime.timeline_selected_keyframe_ids = selected_ids;
    runtime.timeline_selected_keyframe_indices.clear();

    const int channel_count = static_cast<int>(runtime.model.animation_channels.size());
    if (channel_count <= 0) {
        ClearTimelineInteractionState();
        return;
    }

    runtime.timeline_selected_channel_index = std::clamp(runtime.timeline_selected_channel_index, 0, channel_count - 1);
    auto &ch = runtime.model.animation_channels[static_cast<std::size_t>(runtime.timeline_selected_channel_index)];
    NormalizeTimelineSelection(runtime, ch);
    ClearTimelineInteractionState();
}

void ClearTimelineInteractionState() {
    auto &interaction = GetTimelineInteractionStorage();
    interaction.drag_undo_snapshot.clear();
    interaction.drag_undo_selected_ids.clear();
    interaction.drag_snapshot_captured = false;
    interaction.dragging_keyframe_index = -1;
    interaction.dragging_channel_index = -1;
    interaction.box_select_active = false;
    interaction.box_select_start_x = 0.0f;
    interaction.box_select_start_y = 0.0f;
    interaction.box_select_end_x = 0.0f;
    interaction.box_select_end_y = 0.0f;
}

PerceptionPanelState BuildPerceptionPanelState(const AppRuntime &runtime) {
    PerceptionPanelState state{};
    state.capture_ready = runtime.perception_state.screen_capture_ready;
    state.scene_ready = runtime.perception_state.scene_classifier_ready;
    state.ocr_ready = runtime.perception_state.ocr_ready;
    state.facemesh_ready = runtime.perception_state.camera_facemesh_ready;
    state.feature_ocr_enabled = runtime.feature_flags.ocr_enabled;
    state.feature_face_emotion_enabled = runtime.feature_flags.face_emotion_enabled;
    state.face_detected = runtime.perception_state.face_emotion_result.face_detected;
    state.has_face_keypoints = !runtime.perception_state.face_emotion_result.keypoints.empty();
    state.has_ocr_lines = !runtime.perception_state.ocr_result.lines.empty();
    state.ocr_timeout_ms = runtime.perception_state.ocr_timeout_ms;
    state.ocr_det_input_size = runtime.perception_state.ocr_det_input_size;
    state.ocr_det_effective_input_size = runtime.perception_state.ocr_det_input_size;
    state.capture_poll_interval_sec = runtime.perception_state.screen_capture_poll_interval_sec;
    state.capture_success_count = runtime.perception_state.screen_capture_success_count;
    state.capture_fail_count = runtime.perception_state.screen_capture_fail_count;
    state.scene_total_runs = runtime.perception_state.scene_total_runs;
    state.face_total_runs = runtime.perception_state.face_total_runs;
    state.ocr_total_kept_lines = runtime.perception_state.ocr_total_kept_lines;
    state.ocr_total_raw_lines = runtime.perception_state.ocr_total_raw_lines;
    state.ocr_total_dropped_low_conf_lines = runtime.perception_state.ocr_total_dropped_low_conf_lines;
    state.ocr_low_conf_threshold = runtime.perception_state.ocr_low_conf_threshold;
    const double total = static_cast<double>(runtime.perception_state.screen_capture_success_count +
                                             runtime.perception_state.screen_capture_fail_count);
    state.capture_success_rate = total > 0.0
        ? static_cast<double>(runtime.perception_state.screen_capture_success_count) / total
        : 0.0;
    state.scene_avg_latency_ms = runtime.perception_state.scene_avg_latency_ms;
    state.face_avg_latency_ms = runtime.perception_state.face_avg_latency_ms;
    state.ocr_avg_latency_ms = runtime.perception_state.ocr_avg_latency_ms;
    state.ocr_discard_rate = runtime.perception_state.ocr_discard_rate;
    state.scene_label = runtime.perception_state.scene_result.label;
    state.scene_score = runtime.perception_state.scene_result.score;
    state.ocr_summary = runtime.perception_state.ocr_result.summary;
    if (!runtime.perception_state.ocr_result.lines.empty()) {
        state.top_ocr_text = runtime.perception_state.ocr_result.lines.front().text;
        state.top_ocr_score = runtime.perception_state.ocr_result.lines.front().score;
    }
    state.emotion_label = runtime.perception_state.face_emotion_result.emotion_label;
    state.emotion_score = runtime.perception_state.face_emotion_result.emotion_score;
    state.head_yaw_deg = runtime.perception_state.face_emotion_result.head_yaw_deg;
    state.head_pitch_deg = runtime.perception_state.face_emotion_result.head_pitch_deg;
    state.head_roll_deg = runtime.perception_state.face_emotion_result.head_roll_deg;
    state.eye_open_left = runtime.perception_state.face_emotion_result.eye_open_left;
    state.eye_open_right = runtime.perception_state.face_emotion_result.eye_open_right;
    state.eye_open_avg = runtime.perception_state.face_emotion_result.eye_open_avg;
    state.face_keypoint_count = static_cast<int>(runtime.perception_state.face_emotion_result.keypoints.size());
    if (!runtime.perception_state.face_emotion_result.keypoints.empty()) {
        const auto &kp = runtime.perception_state.face_emotion_result.keypoints.front();
        state.first_keypoint_name = kp.name;
        state.first_keypoint_x = kp.x;
        state.first_keypoint_y = kp.y;
        state.first_keypoint_score = kp.score;
    }
    const auto conf_total = runtime.perception_state.ocr_conf_low_count +
                            runtime.perception_state.ocr_conf_mid_count +
                            runtime.perception_state.ocr_conf_high_count;
    state.has_confidence_samples = conf_total > 0;
    if (conf_total > 0) {
        state.ocr_conf_low_pct = static_cast<float>(runtime.perception_state.ocr_conf_low_count) * 100.0f / static_cast<float>(conf_total);
        state.ocr_conf_mid_pct = static_cast<float>(runtime.perception_state.ocr_conf_mid_count) * 100.0f / static_cast<float>(conf_total);
        state.ocr_conf_high_pct = static_cast<float>(runtime.perception_state.ocr_conf_high_count) * 100.0f / static_cast<float>(conf_total);
    }
    state.process_name = runtime.perception_state.system_context_snapshot.process_name;
    state.window_title = runtime.perception_state.system_context_snapshot.window_title;
    state.url_hint = runtime.perception_state.system_context_snapshot.url_hint;
    state.capture_error = runtime.perception_state.screen_capture_last_error;
    state.scene_error = runtime.perception_state.scene_classifier_last_error;
    state.camera_error = runtime.perception_state.camera_facemesh_last_error;
    state.system_context_error = runtime.perception_state.system_context_last_error;
    state.ocr_error = runtime.perception_state.ocr_last_error;
    state.recent_error = !runtime.perception_state.camera_facemesh_last_error.empty() ? runtime.perception_state.camera_facemesh_last_error :
                         !runtime.perception_state.ocr_last_error.empty() ? runtime.perception_state.ocr_last_error :
                         !runtime.perception_state.scene_classifier_last_error.empty() ? runtime.perception_state.scene_classifier_last_error :
                         !runtime.perception_state.system_context_last_error.empty() ? runtime.perception_state.system_context_last_error :
                         runtime.perception_state.screen_capture_last_error;
    return state;
}

AsrChatPanelState BuildAsrChatPanelState(const AppRuntime &runtime) {
    AsrChatPanelState state{};
    state.asr_ready = runtime.asr_ready;
    state.feature_asr_enabled = runtime.feature_flags.asr_enabled;
    state.chat_ready = runtime.chat_ready;
    state.feature_chat_enabled = runtime.feature_flags.chat_enabled;
    state.prefer_cloud_chat = runtime.prefer_cloud_chat;
    state.observability_log_enabled = runtime.observability.log_enabled;
    state.observability_log_interval_sec = runtime.observability.log_interval_sec;
    state.asr_text = runtime.asr_last_result.text;
    state.asr_switch_reason = runtime.asr_last_switch_reason;
    state.asr_last_error = runtime.asr_last_error;
    state.chat_input = runtime.chat_input;
    state.chat_answer = runtime.chat_last_answer;
    state.chat_switch_reason = runtime.chat_last_switch_reason;
    state.chat_last_error = runtime.chat_last_error;
    state.recent_error = !runtime.chat_last_error.empty() ? runtime.chat_last_error :
                         !runtime.asr_last_error.empty() ? runtime.asr_last_error :
                         runtime.plugin.last_error;
    state.asr_rtf = runtime.asr_rtf;
    state.asr_wer_proxy = runtime.asr_wer_proxy;
    state.asr_timeout_rate = runtime.asr_timeout_rate;
    state.asr_cloud_call_ratio = runtime.asr_cloud_call_ratio;
    state.asr_cloud_success_ratio = runtime.asr_cloud_success_ratio;
    state.plugin_update_hz = runtime.plugin.current_update_hz;
    state.plugin_total_updates = runtime.plugin.total_update_count;
    state.plugin_timeout_count = runtime.plugin.timeout_count;
    state.plugin_exception_count = runtime.plugin.exception_count;
    state.plugin_internal_error_count = runtime.plugin.internal_error_count;
    state.plugin_disable_count = runtime.plugin.disable_count;
    state.plugin_recover_count = runtime.plugin.recover_count;
    state.plugin_timeout_rate = runtime.plugin.timeout_rate;
    state.plugin_auto_disabled = runtime.plugin.auto_disabled;
    state.plugin_last_error = runtime.plugin.last_error;
    state.has_plugin_last_error = !runtime.plugin.last_error.empty();
    state.plugin_route_selected = runtime.plugin.route_selected;
    state.plugin_route_scene_score = runtime.plugin.route_scene_score;
    state.plugin_route_task_score = runtime.plugin.route_task_score;
    state.plugin_route_presence_score = runtime.plugin.route_presence_score;
    state.plugin_route_total_score = runtime.plugin.route_total_score;
    state.plugin_route_rejected_summary = runtime.plugin.route_rejected_summary;
    state.has_route_rejected_summary = !runtime.plugin.route_rejected_summary.empty();
    state.can_send_chat = runtime.feature_flags.chat_enabled && runtime.chat_ready && runtime.chat_provider != nullptr;
    return state;
}

void ApplyPerceptionPanelAction(AppRuntime &runtime, const PerceptionPanelAction &action) {
    switch (action.type) {
        case PerceptionPanelActionType::SetOcrTimeoutMs:
            runtime.perception_state.ocr_timeout_ms = action.int_value;
            break;
        case PerceptionPanelActionType::SetOcrDetInputSize:
            runtime.perception_state.ocr_det_input_size = action.int_value;
            break;
        case PerceptionPanelActionType::SetCapturePollIntervalSec:
            runtime.perception_state.screen_capture_poll_interval_sec = action.float_value;
            break;
    }
}

void ApplyAsrChatPanelAction(AppRuntime &runtime, const AsrChatPanelAction &action) {
    switch (action.type) {
        case AsrChatPanelActionType::SetObservabilityLogEnabled:
            runtime.observability.log_enabled = action.bool_value;
            break;
        case AsrChatPanelActionType::SetObservabilityLogIntervalSec:
            runtime.observability.log_interval_sec = action.float_value;
            break;
        case AsrChatPanelActionType::SetChatEnabled:
            runtime.feature_flags.chat_enabled = action.bool_value;
            break;
        case AsrChatPanelActionType::SetPreferCloudChat:
            runtime.prefer_cloud_chat = action.bool_value;
            break;
        case AsrChatPanelActionType::SetChatInput:
            SDL_strlcpy(runtime.chat_input, action.text_value.c_str(), sizeof(runtime.chat_input));
            break;
        case AsrChatPanelActionType::SendChat:
            break;
    }
}

}  // namespace desktoper2D
