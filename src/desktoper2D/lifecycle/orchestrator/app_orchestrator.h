#pragma once

namespace desktoper2D {

struct AppLifecycleContext;

// Orchestrator 层：负责生命周期编排（init/bootstrap/run/teardown 调度）
bool RunAppOrchestrator(AppLifecycleContext &ctx);

}  // namespace desktoper2D
