#include "k2d/lifecycle/runtime_config_applier.h"

#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

void ApplyRuntimeConfig(AppRuntime &runtime, const AppRuntimeConfig &cfg) {
    runtime.click_through = cfg.click_through;
    runtime.window_visible = cfg.window_visible;
    runtime.show_debug_stats = cfg.show_debug_stats;
    runtime.manual_param_mode = cfg.manual_param_mode;
    runtime.dev_hot_reload_enabled = cfg.dev_hot_reload_enabled;
    runtime.plugin_param_blend_mode = cfg.plugin_param_blend_mode;
    runtime.task_category_config = cfg.task_category;
}

}  // namespace k2d
