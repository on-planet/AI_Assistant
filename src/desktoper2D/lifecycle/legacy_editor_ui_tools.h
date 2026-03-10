#pragma once

#include <functional>
#include <string>

namespace desktoper2D {

struct AppRuntime;
struct ModelRuntime;
enum class AxisConstraint;
enum class EditorProp;

void SetEditorStatus(AppRuntime &runtime, std::string text, float ttl_sec = 2.0f);
float QuantizeToGrid(float v, float grid);
const char *AxisConstraintName(AxisConstraint c);
const char *EditorPropName(EditorProp p);
void RenderModelHierarchyTree(ModelRuntime &model,
                              int *selected_part_index,
                              const char *filter_text = nullptr,
                              bool auto_expand_matches = true,
                              const std::function<void(int)> &on_select = {});

void RenderResourceTreeInspector(AppRuntime &runtime,
                                 ModelRuntime &model,
                                 int *selected_part_index,
                                 int *selected_deformer_type,
                                 char *filter_text,
                                 int filter_text_capacity,
                                 bool *auto_expand_matches,
                                 const std::function<void(int)> &on_select = {});

}  // namespace desktoper2D
