#include "desktoper2D/lifecycle/plugin_module.h"

#include <algorithm>

namespace desktoper2D {

bool PluginModuleManager::RegisterModule(std::unique_ptr<IPluginModule> module) {
    if (!module) {
        return false;
    }
    modules_.push_back(std::move(module));
    return true;
}

void PluginModuleManager::SubmitInput(const PluginModuleContext &ctx) {
    latest_ctx_ = ctx;
    for (const auto &module : modules_) {
        module->SubmitInput(ctx);
    }
}

void PluginModuleManager::UpdateModules() {
    PluginModuleContext ctx = latest_ctx_;
    for (auto &module : modules_) {
        module->Update(ctx);
    }
}

std::vector<PluginModuleStats> PluginModuleManager::SnapshotStats() const {
    std::vector<PluginModuleStats> stats;
    stats.reserve(modules_.size());
    for (const auto &module : modules_) {
        stats.push_back(module->GetStats());
    }
    return stats;
}

}  // namespace desktoper2D
