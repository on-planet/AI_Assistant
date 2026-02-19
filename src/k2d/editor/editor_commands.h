#pragma once

#include "k2d/core/model.h"

#include <functional>
#include <string>
#include <vector>

namespace k2d {

struct EditCommand {
    enum class Type {
        Transform,
    };

    Type type = Type::Transform;
    std::string part_id;

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
};

int FindPartIndexById(const ModelRuntime &model, const std::string &id);

void PushEditCommand(std::vector<EditCommand> &undo_stack,
                     std::vector<EditCommand> &redo_stack,
                     EditCommand cmd);

void ApplyEditCommand(ModelRuntime &model,
                      const EditCommand &cmd,
                      bool use_after,
                      const std::function<void(ModelPart *, float, float)> &apply_pivot_delta);

bool UndoLastEdit(ModelRuntime &model,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta);

bool RedoLastEdit(ModelRuntime &model,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta);

}  // namespace k2d

