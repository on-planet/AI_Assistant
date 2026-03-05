#pragma once

#include <string>

namespace k2d {

struct AppRuntime;
struct ModelRuntime;
enum class AxisConstraint;
enum class EditorProp;

void SetEditorStatus(AppRuntime &runtime, std::string text, float ttl_sec = 2.0f);
float QuantizeToGrid(float v, float grid);
const char *AxisConstraintName(AxisConstraint c);
const char *EditorPropName(EditorProp p);
void RenderModelHierarchyTree(ModelRuntime &model, int selected_part_index);

}  // namespace k2d
