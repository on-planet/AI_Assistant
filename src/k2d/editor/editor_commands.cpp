#include "k2d/editor/editor_commands.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace k2d {

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
                      const EditCommand &cmd,
                      bool use_after,
                      const std::function<void(ModelPart *, float, float)> &apply_pivot_delta) {
    const int idx = FindPartIndexById(model, cmd.part_id);
    if (idx < 0 || idx >= static_cast<int>(model.parts.size())) {
        return;
    }

    ModelPart &part = model.parts[static_cast<std::size_t>(idx)];

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
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta) {
    if (undo_stack.empty()) {
        return false;
    }
    EditCommand cmd = undo_stack.back();
    undo_stack.pop_back();
    ApplyEditCommand(model, cmd, false, apply_pivot_delta);
    redo_stack.push_back(std::move(cmd));
    return true;
}

bool RedoLastEdit(ModelRuntime &model,
                  std::vector<EditCommand> &undo_stack,
                  std::vector<EditCommand> &redo_stack,
                  const std::function<void(ModelPart *, float, float)> &apply_pivot_delta) {
    if (redo_stack.empty()) {
        return false;
    }
    EditCommand cmd = redo_stack.back();
    redo_stack.pop_back();
    ApplyEditCommand(model, cmd, true, apply_pivot_delta);
    undo_stack.push_back(std::move(cmd));
    return true;
}

}  // namespace k2d

