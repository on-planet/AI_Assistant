#pragma once

#include "k2d/core/model.h"
#include "k2d/lifecycle/reminder_service.h"

#include <functional>
#include <string>
#include <vector>

namespace k2d {

struct EditCommand {
    enum class Type {
        Transform,
        Timeline,
        Param,
        Inspector,
        Reminder,
    };

    Type type = Type::Transform;
    std::string part_id;
    std::string channel_id;
    std::string param_id;

    float before_pos_x = 0.0f;
    float before_pos_y = 0.0f;
    float after_pos_x = 0.0f;
    float after_pos_y = 0.0f;

    float before_pivot_x = 0.0f;
    float before_pivot_y = 0.0f;
    float after_pivot_x = 0.0f;
    float after_pivot_y = 0.0f;

    float before_rot_deg = 0.0f;
    float after_rot_deg = 0.0f;
    float before_scale_x = 1.0f;
    float before_scale_y = 1.0f;
    float after_scale_x = 1.0f;
    float after_scale_y = 1.0f;

    float before_param_value = 0.0f;
    float after_param_value = 0.0f;
    float before_opacity = 1.0f;
    float after_opacity = 1.0f;
    int before_deformer_type = 0;
    int after_deformer_type = 0;
    float before_rotation_weight = 0.0f;
    float after_rotation_weight = 0.0f;
    float before_rotation_speed = 0.0f;
    float after_rotation_speed = 0.0f;
    float before_warp_weight = 0.0f;
    float after_warp_weight = 0.0f;

    std::int64_t reminder_id = 0;
    ReminderItem before_reminder{};
    ReminderItem after_reminder{};
    bool reminder_had_before = false;
    bool reminder_had_after = false;

    std::vector<TimelineKeyframe> before_keyframes;
    std::vector<TimelineKeyframe> after_keyframes;
    std::vector<std::uint64_t> before_selected_keyframe_ids;
    std::vector<std::uint64_t> after_selected_keyframe_ids;
};

int FindPartIndexById(const ModelRuntime &model, const std::string &id);

void PushEditCommand(std::vector<EditCommand> &undo_stack,
                     std::vector<EditCommand> &redo_stack,
                     EditCommand cmd);

void ApplyEditCommand(ModelRuntime &model,
                      ReminderService *reminder_service,
                      std::vector<ReminderItem> *reminder_items,
                      const EditCommand &cmd,
                      bool use_after,
                      const std::function<void(ModelPart *, float, float)> &apply_pivot_delta,
                      const std::function<void(const std::vector<std::uint64_t> &)> &apply_timeline_selection = {});

bool UndoLastEdit(ModelRuntime &model,
                  ReminderService *reminder_service,
                  std::vector<ReminderItem> *reminder_items,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta,
                  const std::function<void(const std::vector<std::uint64_t> &)> &apply_timeline_selection = {});

bool RedoLastEdit(ModelRuntime &model,
                  ReminderService *reminder_service,
                  std::vector<ReminderItem> *reminder_items,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta,
                  const std::function<void(const std::vector<std::uint64_t> &)> &apply_timeline_selection = {});

}  // namespace k2d

