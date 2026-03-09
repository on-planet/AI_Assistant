#include "k2d/lifecycle/editor/editor_session_service.h"

#include "k2d/core/json.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/lifecycle/state/app_runtime_state.h"
#include "k2d/lifecycle/ui/app_debug_ui_panel_state.h"

#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace k2d {
namespace {

std::filesystem::path ParentPathOrDot(const std::filesystem::path &path) {
    const std::filesystem::path parent = path.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

std::string LexicallyNormalUtf8(const std::filesystem::path &path) {
    return path.lexically_normal().generic_string();
}

std::string BuildProjectSaveAsPath(const AppRuntime &runtime) {
    namespace fs = std::filesystem;
    fs::path current = runtime.current_project_path.empty()
                           ? fs::path("assets/project.json")
                           : fs::path(runtime.current_project_path);

    const fs::path dir = ParentPathOrDot(current);
    const std::string stem = current.stem().empty() ? std::string("project") : current.stem().string();
    const std::string ext = current.has_extension() ? current.extension().string() : std::string(".json");

    fs::path candidate = (dir / (stem + "_copy" + ext)).lexically_normal();
    if (!fs::exists(candidate)) {
        return LexicallyNormalUtf8(candidate);
    }

    for (int i = 2; i <= 999; ++i) {
        candidate = (dir / (stem + "_copy_" + std::to_string(i) + ext)).lexically_normal();
        if (!fs::exists(candidate)) {
            return LexicallyNormalUtf8(candidate);
        }
    }

    return LexicallyNormalUtf8((dir / (stem + "_copy_overflow" + ext)).lexically_normal());
}

}  // namespace

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

namespace {

const char *EditCommandTypeName(EditCommand::Type type) {
    switch (type) {
        case EditCommand::Type::Transform: return "transform";
        case EditCommand::Type::Timeline: return "timeline";
        case EditCommand::Type::Param: return "param";
        case EditCommand::Type::Inspector: return "inspector";
        case EditCommand::Type::Reminder: return "reminder";
        default: return "unknown";
    }
}

void SetUndoRedoStatus(AppRuntime &runtime, const char *op_name, bool ok, const EditCommand *cmd) {
    if (!ok) {
        runtime.editor_status = std::string(op_name) + " empty";
        runtime.editor_status_ttl = 1.0f;
        return;
    }
    runtime.editor_status = std::string(op_name) + " " + EditCommandTypeName(cmd ? cmd->type : EditCommand::Type::Transform);
    runtime.editor_status_ttl = 1.0f;
}

}  // namespace

void UndoLastEdit(AppRuntime &runtime) {
    const EditCommand *cmd = runtime.undo_stack.empty() ? nullptr : &runtime.undo_stack.back();
    const bool ok = k2d::UndoLastEdit(
        runtime.model,
        &runtime.reminder_service,
        &runtime.reminder_upcoming,
        runtime.undo_stack,
        runtime.redo_stack,
        [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); },
        [&](const std::vector<std::uint64_t> &selected_ids) {
            RestoreTimelineSelectionFromHistory(runtime, selected_ids);
        });
    SetUndoRedoStatus(runtime, "undo", ok, cmd);
}

void RedoLastEdit(AppRuntime &runtime) {
    const EditCommand *cmd = runtime.redo_stack.empty() ? nullptr : &runtime.redo_stack.back();
    const bool ok = k2d::RedoLastEdit(
        runtime.model,
        &runtime.reminder_service,
        &runtime.reminder_upcoming,
        runtime.undo_stack,
        runtime.redo_stack,
        [](ModelPart *part, float dx, float dy) { ApplyPivotDelta(part, dx, dy); },
        [&](const std::vector<std::uint64_t> &selected_ids) {
            RestoreTimelineSelectionFromHistory(runtime, selected_ids);
        });
    SetUndoRedoStatus(runtime, "redo", ok, cmd);
}

bool SaveEditorProjectJsonToDisk(AppRuntime &runtime, const std::string &project_path, std::string *out_error) {
    if (out_error) out_error->clear();
    if (!runtime.model_loaded) {
        if (out_error) *out_error = "model not loaded";
        return false;
    }

    const std::filesystem::path project_fs_path(project_path);
    const std::filesystem::path project_dir = ParentPathOrDot(project_fs_path);

    const std::filesystem::path model_existing_path(runtime.model.model_path.empty()
                                                        ? std::filesystem::path("assets/model_01/model.json")
                                                        : std::filesystem::path(runtime.model.model_path));
    const std::filesystem::path model_out_path_fs = model_existing_path.is_absolute()
                                                        ? model_existing_path
                                                        : (project_dir / model_existing_path).lexically_normal();
    const std::string model_out_path = LexicallyNormalUtf8(model_out_path_fs);

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
    editor.emplace("workspaceMode", JsonValue::makeNumber(static_cast<double>(runtime.workspace_mode == WorkspaceMode::Animation ? 0 :
                                                                              (runtime.workspace_mode == WorkspaceMode::Debug ? 1 :
                                                                              (runtime.workspace_mode == WorkspaceMode::Perception ? 2 : 3)))));
    editor.emplace("workspaceLayoutMode", JsonValue::makeNumber(static_cast<double>(runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset ? 0 : 1)));
    editor.emplace("workspaceManualDockingIni", JsonValue::makeString(runtime.workspace_manual_docking_ini));
    JsonObject workspace_windows;
    workspace_windows.emplace("workspace", JsonValue::makeBool(runtime.show_workspace_window));
    workspace_windows.emplace("overview", JsonValue::makeBool(runtime.show_overview_window));
    workspace_windows.emplace("editor", JsonValue::makeBool(runtime.show_editor_window));
    workspace_windows.emplace("timeline", JsonValue::makeBool(runtime.show_timeline_window));
    workspace_windows.emplace("perception", JsonValue::makeBool(runtime.show_perception_window));
    workspace_windows.emplace("mapping", JsonValue::makeBool(runtime.show_mapping_window));
    workspace_windows.emplace("asrChat", JsonValue::makeBool(runtime.show_asr_chat_window));
    workspace_windows.emplace("errors", JsonValue::makeBool(runtime.show_error_window));
    workspace_windows.emplace("ops", JsonValue::makeBool(runtime.show_ops_window));
    workspace_windows.emplace("inspector", JsonValue::makeBool(runtime.show_inspector_window));
    workspace_windows.emplace("reminder", JsonValue::makeBool(runtime.show_reminder_window));
    editor.emplace("workspaceWindows", JsonValue::makeObject(std::move(workspace_windows)));

    auto save_window_layout = [](const AppRuntime::WindowLayoutState &layout) {
        JsonObject obj;
        obj.emplace("posX", JsonValue::makeNumber(layout.pos_x));
        obj.emplace("posY", JsonValue::makeNumber(layout.pos_y));
        obj.emplace("sizeW", JsonValue::makeNumber(layout.size_w));
        obj.emplace("sizeH", JsonValue::makeNumber(layout.size_h));
        obj.emplace("collapsed", JsonValue::makeBool(layout.collapsed));
        obj.emplace("initialized", JsonValue::makeBool(layout.initialized));
        return JsonValue::makeObject(std::move(obj));
    };

    editor.emplace("runtimeDebugWindow", save_window_layout(runtime.runtime_debug_window_layout));
    editor.emplace("inspectorWindow", save_window_layout(runtime.inspector_window_layout));
    editor.emplace("reminderWindow", save_window_layout(runtime.reminder_window_layout));
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
        const int workspace_layout_mode = static_cast<int>(editor->getNumber("workspaceLayoutMode").value_or(0.0));
        runtime.workspace_layout_mode = workspace_layout_mode == 1 ? WorkspaceLayoutMode::Manual : WorkspaceLayoutMode::Preset;
        runtime.workspace_preset_apply_requested = runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset;
        runtime.workspace_dock_rebuild_requested = runtime.workspace_layout_mode == WorkspaceLayoutMode::Preset;
        runtime.workspace_manual_layout_reset_requested = false;
        runtime.workspace_manual_docking_ini = editor->getString("workspaceManualDockingIni").value_or(runtime.workspace_manual_docking_ini);
        runtime.workspace_manual_layout_pending_load = runtime.workspace_layout_mode == WorkspaceLayoutMode::Manual &&
                                                       !runtime.workspace_manual_docking_ini.empty();
        runtime.editor_view_pan_x = static_cast<float>(editor->getNumber("viewPanX").value_or(runtime.editor_view_pan_x));
        runtime.editor_view_pan_y = static_cast<float>(editor->getNumber("viewPanY").value_or(runtime.editor_view_pan_y));
        runtime.editor_view_zoom = static_cast<float>(editor->getNumber("viewZoom").value_or(runtime.editor_view_zoom));
        const int workspace_mode = static_cast<int>(editor->getNumber("workspaceMode").value_or(1.0));
        runtime.workspace_mode = workspace_mode == 0 ? WorkspaceMode::Animation :
                                 (workspace_mode == 1 ? WorkspaceMode::Debug :
                                 (workspace_mode == 2 ? WorkspaceMode::Perception : WorkspaceMode::Authoring));
        runtime.last_applied_workspace_mode = static_cast<WorkspaceMode>(-1);

        auto load_window_layout = [](const JsonValue *value, AppRuntime::WindowLayoutState &layout) {
            if (!value || !value->isObject()) {
                return;
            }
            layout.pos_x = static_cast<float>(value->getNumber("posX").value_or(layout.pos_x));
            layout.pos_y = static_cast<float>(value->getNumber("posY").value_or(layout.pos_y));
            layout.size_w = static_cast<float>(value->getNumber("sizeW").value_or(layout.size_w));
            layout.size_h = static_cast<float>(value->getNumber("sizeH").value_or(layout.size_h));
            layout.collapsed = value->getBool("collapsed").value_or(layout.collapsed);
            layout.initialized = value->getBool("initialized").value_or(layout.initialized);
        };
        auto load_window_visible = [](const JsonValue *value, const char *key, bool current) {
            if (!value || !value->isObject()) {
                return current;
            }
            return value->getBool(key).value_or(current);
        };

        load_window_layout(editor->get("runtimeDebugWindow"), runtime.runtime_debug_window_layout);
        load_window_layout(editor->get("inspectorWindow"), runtime.inspector_window_layout);
        load_window_layout(editor->get("reminderWindow"), runtime.reminder_window_layout);
        const JsonValue *workspace_windows = editor->get("workspaceWindows");
        runtime.show_workspace_window = load_window_visible(workspace_windows, "workspace", runtime.show_workspace_window);
        runtime.show_overview_window = load_window_visible(workspace_windows, "overview", runtime.show_overview_window);
        runtime.show_editor_window = load_window_visible(workspace_windows, "editor", runtime.show_editor_window);
        runtime.show_timeline_window = load_window_visible(workspace_windows, "timeline", runtime.show_timeline_window);
        runtime.show_perception_window = load_window_visible(workspace_windows, "perception", runtime.show_perception_window);
        runtime.show_mapping_window = load_window_visible(workspace_windows, "mapping", runtime.show_mapping_window);
        runtime.show_asr_chat_window = load_window_visible(workspace_windows, "asrChat", runtime.show_asr_chat_window);
        runtime.show_error_window = load_window_visible(workspace_windows, "errors", runtime.show_error_window);
        runtime.show_ops_window = load_window_visible(workspace_windows, "ops", runtime.show_ops_window);
        runtime.show_inspector_window = load_window_visible(workspace_windows, "inspector", runtime.show_inspector_window);
        runtime.show_reminder_window = load_window_visible(workspace_windows, "reminder", runtime.show_reminder_window);
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

void SaveEditorProjectAsToDisk(AppRuntime &runtime) {
    const std::string path = BuildProjectSaveAsPath(runtime);
    std::string err;
    if (SaveEditorProjectJsonToDisk(runtime, path, &err)) {
        runtime.current_project_path = path;
        runtime.editor_status = "saved project as: " + path;
        runtime.editor_status_ttl = 2.5f;
    } else {
        runtime.editor_status = "save project as failed: " + err;
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
