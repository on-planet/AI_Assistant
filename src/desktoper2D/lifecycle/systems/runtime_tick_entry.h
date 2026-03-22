#pragma once

#include <functional>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

struct ModelReloadServiceContext;
struct BehaviorApplyContext;

struct RuntimeTickBridge {
    using PickTopPartAtFn = std::function<int(float, float)>;
    using HasModelParamsFn = std::function<bool()>;
    using BuildModelReloadContextFn = std::function<ModelReloadServiceContext()>;
    using BuildBehaviorApplyContextFn = std::function<BehaviorApplyContext()>;
    using TaskSecondaryCategoryNameFn = std::function<const char *(TaskSecondaryCategory)>;

    PickTopPartAtFn pick_top_part_at;
    HasModelParamsFn has_model_params;
    BuildModelReloadContextFn build_model_reload_context;
    BuildBehaviorApplyContextFn build_behavior_apply_context;
    TaskSecondaryCategoryNameFn task_secondary_category_name;

    const char *TaskSecondaryCategoryName(TaskSecondaryCategory category) const {
        return task_secondary_category_name ? task_secondary_category_name(category) : "unknown";
    }
};

void RunRuntimeTickEntry(AppRuntime &runtime, float dt, const RuntimeTickBridge &bridge);

}  // namespace desktoper2D
