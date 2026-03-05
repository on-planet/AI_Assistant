#include "k2d/lifecycle/editor/editor_session_service.h"

#include "k2d/core/json.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/lifecycle/state/app_runtime_state.h"

#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <filesystem>
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

bool SaveEditorProjectJsonToDisk(AppRuntime &runtime, const std::string &project_path, std::string *out_error) {
    if (out_error) out_error->clear();
    if (!runtime.model_loaded) {
        if (out_error) *out_error = "model not loaded";
        return false;
    }

    const std::string model_out_path = runtime.model.model_path.empty()
                                       ? std::string("assets/model_01/model.json")
                                       : runtime.model.model_path;

    std::string model_err;
    if (!SaveModelRuntimeJson(runtime.model, model_out_path.c_str(), &model_err)) {
        if (out_error) *out_error = "save model json failed: " + model_err;
        return false;
    }
    runtime.model.model_path = model_out_path;

    JsonObject root;
    root.emplace("schema", JsonValue::makeString("k2d.editor.project.v1"));
    root.emplace("version", JsonValue::makeNumber(1.0));

    JsonObject model;
    model.emplace("path", JsonValue::makeString(model_out_path));
    root.emplace("model", JsonValue::makeObject(std::move(model)));

    JsonObject editor;
    editor.emplace("selectedPartIndex", JsonValue::makeNumber(static_cast<double>(runtime.selected_part_index)));
    editor.emplace("selectedParamIndex", JsonValue::makeNumber(static_cast<double>(runtime.selected_param_index)));
    editor.emplace("manualParamMode", JsonValue::makeBool(runtime.manual_param_mode));
    editor.emplace("editMode", JsonValue::makeBool(runtime.edit_mode));
    editor.emplace("viewPanX", JsonValue::makeNumber(runtime.editor_view_pan_x));
    editor.emplace("viewPanY", JsonValue::makeNumber(runtime.editor_view_pan_y));
    editor.emplace("viewZoom", JsonValue::makeNumber(runtime.editor_view_zoom));
    root.emplace("editor", JsonValue::makeObject(std::move(editor)));

    JsonObject feature;
    feature.emplace("sceneClassifierEnabled", JsonValue::makeBool(runtime.feature_scene_classifier_enabled));
    feature.emplace("ocrEnabled", JsonValue::makeBool(runtime.feature_ocr_enabled));
    feature.emplace("faceEmotionEnabled", JsonValue::makeBool(runtime.feature_face_emotion_enabled));
    feature.emplace("faceParamMappingEnabled", JsonValue::makeBool(runtime.feature_face_param_mapping_enabled));
    feature.emplace("asrEnabled", JsonValue::makeBool(runtime.feature_asr_enabled));
    feature.emplace("chatEnabled", JsonValue::makeBool(runtime.feature_chat_enabled));
    root.emplace("feature", JsonValue::makeObject(std::move(feature)));

    const std::string text = StringifyJson(JsonValue::makeObject(std::move(root)), 2);
    SDL_IOStream *io = SDL_IOFromFile(project_path.c_str(), "wb");
    if (!io) {
        if (out_error) *out_error = std::string("open project file failed: ") + SDL_GetError();
        return false;
    }

    const size_t need = text.size();
    const size_t wrote = SDL_WriteIO(io, text.data(), need);
    SDL_CloseIO(io);
    if (wrote != need) {
        if (out_error) *out_error = "write project file failed";
        return false;
    }
    return true;
}

bool LoadEditorProjectJsonFromDisk(AppRuntime &runtime,
                                   SDL_Renderer *renderer,
                                   const std::string &project_path,
                                   std::string *out_error) {
    if (out_error) out_error->clear();

    SDL_IOStream *io = SDL_IOFromFile(project_path.c_str(), "rb");
    if (!io) {
        if (out_error) *out_error = std::string("open project file failed: ") + SDL_GetError();
        return false;
    }

    const Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        if (out_error) *out_error = "project file is empty";
        return false;
    }

    std::string text(static_cast<std::size_t>(sz), '\0');
    const size_t read_n = SDL_ReadIO(io, text.data(), static_cast<size_t>(sz));
    SDL_CloseIO(io);
    if (read_n != static_cast<size_t>(sz)) {
        if (out_error) *out_error = "read project file failed";
        return false;
    }

    JsonParseError jerr{};
    auto root_opt = ParseJson(text, &jerr);
    if (!root_opt || !root_opt->isObject()) {
        if (out_error) {
            *out_error = "project json parse failed at line " + std::to_string(jerr.line) +
                         ", col " + std::to_string(jerr.column) + ": " + jerr.message;
        }
        return false;
    }

    const JsonValue &root = *root_opt;
    const std::string schema = root.getString("schema").value_or(std::string());
    if (schema != "k2d.editor.project.v1") {
        if (out_error) *out_error = "unsupported project schema: " + schema;
        return false;
    }

    const JsonValue *model_v = root.get("model");
    const std::string model_path = (model_v && model_v->isObject())
                                   ? model_v->getString("path").value_or(std::string())
                                   : std::string();
    if (model_path.empty()) {
        if (out_error) *out_error = "project.model.path is missing";
        return false;
    }

    ModelRuntime loaded;
    std::string model_err;
    if (!LoadModelRuntime(renderer, model_path.c_str(), &loaded, &model_err)) {
        if (out_error) *out_error = "load model from project failed: " + model_err;
        return false;
    }

    DestroyModelRuntime(&runtime.model);
    runtime.model = std::move(loaded);
    runtime.model_loaded = true;
    runtime.model_time = 0.0f;

    if (const JsonValue *editor = root.get("editor"); editor && editor->isObject()) {
        runtime.selected_part_index = static_cast<int>(editor->getNumber("selectedPartIndex").value_or(-1.0));
        runtime.selected_param_index = static_cast<int>(editor->getNumber("selectedParamIndex").value_or(0.0));
        runtime.manual_param_mode = editor->getBool("manualParamMode").value_or(runtime.manual_param_mode);
        runtime.edit_mode = editor->getBool("editMode").value_or(runtime.edit_mode);
        runtime.editor_view_pan_x = static_cast<float>(editor->getNumber("viewPanX").value_or(runtime.editor_view_pan_x));
        runtime.editor_view_pan_y = static_cast<float>(editor->getNumber("viewPanY").value_or(runtime.editor_view_pan_y));
        runtime.editor_view_zoom = static_cast<float>(editor->getNumber("viewZoom").value_or(runtime.editor_view_zoom));
    }

    if (const JsonValue *feature = root.get("feature"); feature && feature->isObject()) {
        runtime.feature_scene_classifier_enabled = feature->getBool("sceneClassifierEnabled").value_or(runtime.feature_scene_classifier_enabled);
        runtime.feature_ocr_enabled = feature->getBool("ocrEnabled").value_or(runtime.feature_ocr_enabled);
        runtime.feature_face_emotion_enabled = feature->getBool("faceEmotionEnabled").value_or(runtime.feature_face_emotion_enabled);
        runtime.feature_face_param_mapping_enabled = feature->getBool("faceParamMappingEnabled").value_or(runtime.feature_face_param_mapping_enabled);
        runtime.feature_asr_enabled = feature->getBool("asrEnabled").value_or(runtime.feature_asr_enabled);
        runtime.feature_chat_enabled = feature->getBool("chatEnabled").value_or(runtime.feature_chat_enabled);
    }

    runtime.model.animation_channels_enabled = !runtime.manual_param_mode;
    runtime.current_project_path = project_path;
    return true;
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

void SaveEditorProjectToDisk(AppRuntime &runtime) {
    const std::string path = runtime.current_project_path.empty() ? "assets/project.json" : runtime.current_project_path;
    std::string err;
    if (SaveEditorProjectJsonToDisk(runtime, path, &err)) {
        runtime.current_project_path = path;
        runtime.editor_status = "saved project: " + path;
        runtime.editor_status_ttl = 2.5f;
    } else {
        runtime.editor_status = "save project failed: " + err;
        runtime.editor_status_ttl = 3.5f;
    }
}

void LoadEditorProjectFromDisk(AppRuntime &runtime) {
    const std::string path = runtime.current_project_path.empty() ? "assets/project.json" : runtime.current_project_path;
    std::string err;
    if (LoadEditorProjectJsonFromDisk(runtime, runtime.renderer, path, &err)) {
        runtime.editor_status = "loaded project: " + path;
        runtime.editor_status_ttl = 2.5f;
    } else {
        runtime.editor_status = "load project failed: " + err;
        runtime.editor_status_ttl = 3.5f;
    }
}

}  // namespace k2d
