#include "k2d/lifecycle/editor/editor_session_service.h"

#include "k2d/editor/editor_commands.h"
#include "k2d/lifecycle/state/app_runtime_state.h"

#include <algorithm>
#include <string>

namespace k2d {

void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y) {
    if (!part) {
        return;
    }

    part->pivot_x += delta_x;
    part->pivot_y += delta_y;

    part->base_pos_x += delta_x;
    part->base_pos_y += delta_y;

    for (std::size_t i = 0; i + 1 < part->mesh.positions.size(); i += 2) {
        part->mesh.positions[i] -= delta_x;
        part->mesh.positions[i + 1] -= delta_y;
    }

    if (part->deformed_positions.size() == part->mesh.positions.size()) {
        for (std::size_t i = 0; i + 1 < part->deformed_positions.size(); i += 2) {
            part->deformed_positions[i] -= delta_x;
            part->deformed_positions[i + 1] -= delta_y;
        }
    }
}

void UndoLastEdit(AppRuntime &runtime) {
    const bool ok = k2d::UndoLastEdit(
        runtime.model,
        runtime.undo_stack,
        runtime.redo_stack,
        [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); });
    runtime.editor_status = ok ? "undo" : "undo empty";
    runtime.editor_status_ttl = 1.0f;
}

void RedoLastEdit(AppRuntime &runtime) {
    const bool ok = k2d::RedoLastEdit(
        runtime.model,
        runtime.undo_stack,
        runtime.redo_stack,
        [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); });
    runtime.editor_status = ok ? "redo" : "redo empty";
    runtime.editor_status_ttl = 1.0f;
}

void SaveEditedModelJsonToDisk(AppRuntime &runtime) {
    if (!runtime.model_loaded) {
        runtime.editor_status = "save failed: model not loaded";
        runtime.editor_status_ttl = 2.0f;
        return;
    }

    const std::string out_path = runtime.model.model_path.empty() ?
                                 "assets/model_01/model.json" : runtime.model.model_path;

    std::string err;
    const bool ok = SaveModelRuntimeJson(runtime.model, out_path.c_str(), &err);
    if (ok) {
        runtime.editor_status = "saved model json: " + out_path;
        runtime.editor_status_ttl = 2.5f;
    } else {
        runtime.editor_status = "save failed: " + err;
        runtime.editor_status_ttl = 3.5f;
    }
}

}  // namespace k2d
