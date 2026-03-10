#include "desktoper2D/lifecycle/orchestrator/app_orchestrator.h"

#include "desktoper2D/lifecycle/app_lifecycle.h"

namespace desktoper2D {

bool RunAppOrchestrator(AppLifecycleContext &ctx) {
    if (!AppLifecycleInit(ctx)) return false;
    if (!AppLifecycleBootstrap(ctx)) return false;
    AppLifecycleRun(ctx);
    AppLifecycleTeardown(ctx);
    return true;
}

}  // namespace desktoper2D
