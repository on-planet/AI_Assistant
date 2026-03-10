#pragma once

#include <SDL3/SDL.h>

#include "desktoper2D/core/model.h"

#include <functional>

namespace desktoper2D {

struct AppRuntime;

struct RuntimeRenderBridge {
    using BoolFn = std::function<bool()>;
    using VoidFn = std::function<void()>;
    using ComputePartAabbFn = std::function<bool(const ModelPart &, SDL_FRect *)>;
    using NameFn = std::function<const char *()>;

    BoolFn has_model_parts;
    BoolFn has_model_params;
    VoidFn ensure_selected_part_index_valid;
    VoidFn ensure_selected_param_index_valid;
    ComputePartAabbFn compute_part_aabb;

    VoidFn render_model_hierarchy_tree;
    VoidFn render_resource_tree_inspector;
    NameFn task_primary_category_name;
    NameFn task_secondary_category_name;

    const char *TaskPrimaryCategoryName() const {
        return task_primary_category_name ? task_primary_category_name() : "unknown";
    }

    const char *TaskSecondaryCategoryName() const {
        return task_secondary_category_name ? task_secondary_category_name() : "unknown";
    }

    void RenderModelHierarchyTree() const {
        if (render_model_hierarchy_tree) {
            render_model_hierarchy_tree();
        }
    }
    void RenderResourceTreeInspector() const {
        if (render_resource_tree_inspector) {
            render_resource_tree_inspector();
        }
    }
};

void RunRuntimeRenderEntry(AppRuntime &runtime, const RuntimeRenderBridge &bridge);

}  // namespace desktoper2D
