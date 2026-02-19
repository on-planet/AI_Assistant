#include "k2d/lifecycle/app.h"

#include "k2d/lifecycle/app_lifecycle.h"

namespace k2d {

int RunOverlayApp(int argc, char *argv[]) {
    AppLifecycleContext ctx{};
    ctx.argc = argc;
    ctx.argv = argv;

    if (!AppLifecycleInit(ctx)) {
        return ctx.exit_code;
    }

    if (!AppLifecycleBootstrap(ctx)) {
        AppLifecycleTeardown(ctx);
        return ctx.exit_code;
    }

    AppLifecycleRun(ctx);
    AppLifecycleTeardown(ctx);
    return ctx.exit_code;
}

}  // namespace k2d

