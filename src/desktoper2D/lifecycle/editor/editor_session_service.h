#pragma once

#include "desktoper2D/core/model.h"

namespace desktoper2D {

struct AppRuntime;

void ApplyPivotDelta(ModelPart *part, float delta_x, float delta_y);
void UndoLastEdit(AppRuntime &runtime);
void RedoLastEdit(AppRuntime &runtime);

bool SaveEditorProjectJsonToDisk(AppRuntime &runtime, const std::string &project_path, std::string *out_error = nullptr);
bool LoadEditorProjectJsonFromDisk(AppRuntime &runtime,
                                   const std::string &project_path,
                                   std::string *out_error = nullptr);

bool SaveEditorAutosaveProject(AppRuntime &runtime, std::string *out_error = nullptr);
bool LoadEditorAutosaveProject(AppRuntime &runtime, std::string *out_error = nullptr);
bool ClearEditorAutosaveProject(AppRuntime &runtime, std::string *out_error = nullptr);
void RefreshEditorAutosaveState(AppRuntime &runtime);

void SaveEditedModelJsonToDisk(AppRuntime &runtime);
void SaveEditorProjectToDisk(AppRuntime &runtime);
void SaveEditorProjectAsToDisk(AppRuntime &runtime);
void LoadEditorProjectFromDisk(AppRuntime &runtime);

}  // namespace desktoper2D
