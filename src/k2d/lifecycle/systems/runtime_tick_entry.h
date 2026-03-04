#pragma once

#include <functional>

#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

struct ModelReloadServiceContext;
struct BehaviorApplyContext;

struct RuntimeTickBridge {
    using PickTopPartAtFn = std::function<int(float, float)>;
    using HasModelParamsFn = std::function<bool()>;
    using BuildModelReloadContextFn = std::function<ModelReloadServiceContext()>;
    using BuildBehaviorApplyContextFn = std::function<BehaviorApplyContext()>;
    using TaskSecondaryCategoryNameFn = std::function<const char *(TaskSecondaryCategory)>;
    using VoidFn = std::function<void()>;

    PickTopPartAtFn pick_top_part_at;
    HasModelParamsFn has_model_params;
    BuildModelReloadContextFn build_model_reload_context;
    BuildBehaviorApplyContextFn build_behavior_apply_context;
    TaskSecondaryCategoryNameFn task_secondary_category_name;
    VoidFn infer_task_category_inplace;

    const char *TaskSecondaryCategoryName(TaskSecondaryCategory category) const {
        return task_secondary_category_name ? task_secondary_category_name(category) : "unknown";
    }

    void InferTaskCategoryInplace() const {
        if (infer_task_category_inplace) {
            infer_task_category_inplace();
        }
    }
};

void RunRuntimeTickEntry(AppRuntime &runtime, float dt, const RuntimeTickBridge &bridge);

}  // namespace k2d
