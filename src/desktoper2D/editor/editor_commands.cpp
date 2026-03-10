#include "desktoper2D/editor/editor_commands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace desktoper2D {

int FindPartIndexById(const ModelRuntime &model, const std::string &id) {
    auto it = model.part_index.find(id);
    if (it == model.part_index.end()) {
        return -1;
    }
    return it->second;
}

void PushEditCommand(std::vector<EditCommand> &undo_stack,
                     std::vector<EditCommand> &redo_stack,
                     EditCommand cmd) {
    undo_stack.push_back(std::move(cmd));
    redo_stack.clear();
}

void ApplyEditCommand(ModelRuntime &model,
                      ReminderService *reminder_service,
                      std::vector<ReminderItem> *reminder_items,
                      const EditCommand &cmd,
                      bool use_after,
                      const std::function<void(ModelPart *, float, float)> &apply_pivot_delta,
                      const std::function<void(const std::vector<std::uint64_t> &)> &apply_timeline_selection) {
    if (cmd.type == EditCommand::Type::Timeline) {
        for (auto &channel : model.animation_channels) {
            if (channel.id == cmd.channel_id) {
                channel.keyframes = use_after ? cmd.after_keyframes : cmd.before_keyframes;
                std::sort(channel.keyframes.begin(), channel.keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
                    return a.time_sec < b.time_sec;
                });
                if (apply_timeline_selection) {
                    apply_timeline_selection(use_after ? cmd.after_selected_keyframe_ids : cmd.before_selected_keyframe_ids);
                }
                return;
            }
        }
        if (apply_timeline_selection) {
            apply_timeline_selection({});
        }
        return;
    }

    if (cmd.type == EditCommand::Type::Param) {
        auto it = model.param_index.find(cmd.param_id);
        if (it == model.param_index.end()) {
            return;
        }
        const int idx = it->second;
        if (idx < 0 || idx >= static_cast<int>(model.parameters.size())) {
            return;
        }
        auto &param = model.parameters[static_cast<std::size_t>(idx)].param;
        param.SetTarget(use_after ? cmd.after_param_value : cmd.before_param_value);
        param.SetValueImmediate(use_after ? cmd.after_param_value : cmd.before_param_value);
        return;
    }

    if (cmd.type == EditCommand::Type::Reminder) {
        if (!reminder_service) {
            return;
        }

        std::string err;
        const ReminderItem &target = use_after ? cmd.after_reminder : cmd.before_reminder;
        const ReminderItem &other = use_after ? cmd.before_reminder : cmd.after_reminder;
        const bool has_target = use_after ? cmd.reminder_had_after : cmd.reminder_had_before;
        const bool has_other = use_after ? cmd.reminder_had_before : cmd.reminder_had_after;

        auto refresh_items = [&]() {
            if (reminder_items) {
                *reminder_items = reminder_service->ListActive(64, nullptr);
            }
        };

        if (!has_target) {
            if (has_other) {
                reminder_service->DeleteReminder(other.id, &err);
            }
            refresh_items();
            return;
        }

        ReminderItem existing{};
        const bool exists = reminder_service->GetReminderById(target.id, &existing, nullptr);
        if (!exists) {
            if (!reminder_service->RestoreReminder(target, &err)) {
                refresh_items();
                return;
            }
            refresh_items();
            return;
        }

        if (existing.completed != target.completed) {
            reminder_service->MarkCompleted(existing.id, target.completed, &err);
        }
        refresh_items();
        return;
    }

    const int idx = FindPartIndexById(model, cmd.part_id);
    if (idx < 0 || idx >= static_cast<int>(model.parts.size())) {
        return;
    }

    ModelPart &part = model.parts[static_cast<std::size_t>(idx)];

    if (cmd.type == EditCommand::Type::Inspector) {
        part.base_opacity = use_after ? cmd.after_opacity : cmd.before_opacity;
        part.deformer_type = (use_after ? cmd.after_deformer_type : cmd.before_deformer_type) == 1
                                 ? DeformerType::Rotation
                                 : DeformerType::Warp;
        part.rotation_deformer_weight = use_after ? cmd.after_rotation_weight : cmd.before_rotation_weight;
        part.rotation_deformer_speed = use_after ? cmd.after_rotation_speed : cmd.before_rotation_speed;
        if (part.deformer_type == DeformerType::Warp) {
            part.ffd.weight.SetValueImmediate(use_after ? cmd.after_warp_weight : cmd.before_warp_weight);
        }
    }

    const float target_pivot_x = use_after ? cmd.after_pivot_x : cmd.before_pivot_x;
    const float target_pivot_y = use_after ? cmd.after_pivot_y : cmd.before_pivot_y;
    const float dx = target_pivot_x - part.pivot_x;
    const float dy = target_pivot_y - part.pivot_y;
    apply_pivot_delta(&part, dx, dy);

    part.base_pos_x = use_after ? cmd.after_pos_x : cmd.before_pos_x;
    part.base_pos_y = use_after ? cmd.after_pos_y : cmd.before_pos_y;
    part.base_rot_deg = use_after ? cmd.after_rot_deg : cmd.before_rot_deg;
    part.base_scale_x = std::max(0.05f, use_after ? cmd.after_scale_x : cmd.before_scale_x);
    part.base_scale_y = std::max(0.05f, use_after ? cmd.after_scale_y : cmd.before_scale_y);
}

bool UndoLastEdit(ModelRuntime &model,
                  ReminderService *reminder_service,
                  std::vector<ReminderItem> *reminder_items,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta,
                  const std::function<void(const std::vector<std::uint64_t> &)> &apply_timeline_selection) {
    if (undo_stack.empty()) {
        return false;
    }
    EditCommand cmd = undo_stack.back();
    undo_stack.pop_back();
    ApplyEditCommand(model, reminder_service, reminder_items, cmd, false, apply_pivot_delta, apply_timeline_selection);
    redo_stack.push_back(std::move(cmd));
    return true;
}

bool RedoLastEdit(ModelRuntime &model,
                  ReminderService *reminder_service,
                  std::vector<ReminderItem> *reminder_items,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta,
                  const std::function<void(const std::vector<std::uint64_t> &)> &apply_timeline_selection) {
    if (redo_stack.empty()) {
        return false;
    }
    EditCommand cmd = redo_stack.back();
    redo_stack.pop_back();
    ApplyEditCommand(model, reminder_service, reminder_items, cmd, true, apply_pivot_delta, apply_timeline_selection);
    undo_stack.push_back(std::move(cmd));
    return true;
}

}  // namespace desktoper2D

