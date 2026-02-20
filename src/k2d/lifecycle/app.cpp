#include "k2d/lifecycle/app.h"

#include "k2d/lifecycle/app_lifecycle.h"
#include "k2d/core/async_logger.h"

namespace k2d {

int RunOverlayApp(int argc, char *argv[]) {
    AppLifecycleContext ctx{};
    ctx.argc = argc;
    ctx.argv = argv;

    InitProcessLogger();
    LogInfo("application startup");

    if (!AppLifecycleInit(ctx)) {
        LogError("AppLifecycleInit failed, exit_code=%d", ctx.exit_code);
        ShutdownProcessLogger();
        return ctx.exit_code;
    }

    if (!AppLifecycleBootstrap(ctx)) {
        LogError("AppLifecycleBootstrap failed, exit_code=%d", ctx.exit_code);
        AppLifecycleTeardown(ctx);
        ShutdownProcessLogger();
        return ctx.exit_code;
    }

    AppLifecycleRun(ctx);
    AppLifecycleTeardown(ctx);
    LogInfo("application exit, code=%d", ctx.exit_code);
    ShutdownProcessLogger();
    return ctx.exit_code;
}

}  // namespace k2d

