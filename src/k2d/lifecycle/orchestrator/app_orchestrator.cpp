#include "k2d/lifecycle/orchestrator/app_orchestrator.h"

#include "k2d/lifecycle/app_lifecycle.h"

namespace k2d {

bool RunAppOrchestrator(AppLifecycleContext &ctx) {
    if (!AppLifecycleInit(ctx)) return false;
    if (!AppLifecycleBootstrap(ctx)) return false;
    AppLifecycleRun(ctx);
    AppLifecycleTeardown(ctx);
    return true;
}

}  // namespace k2d
