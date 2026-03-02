#pragma once

namespace k2d {

struct AppLifecycleContext;

// Orchestrator 层：负责生命周期编排（init/bootstrap/run/teardown 调度）
bool RunAppOrchestrator(AppLifecycleContext &ctx);

}  // namespace k2d
