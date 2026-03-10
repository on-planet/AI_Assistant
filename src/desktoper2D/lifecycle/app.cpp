#include "desktoper2D/lifecycle/app.h"

#include "desktoper2D/lifecycle/app_lifecycle.h"
#include "desktoper2D/core/async_logger.h"
#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

namespace {

bool DefaultInit(AppRunContext &ctx) {
    AppLifecycleContext lifecycle_ctx{};
    lifecycle_ctx.argc = ctx.argc;
    lifecycle_ctx.argv = ctx.argv;
    lifecycle_ctx.exit_code = ctx.exit_code;

    lifecycle_ctx.runtime = &g_runtime;
    const bool ok = AppLifecycleInit(lifecycle_ctx);
    ctx.exit_code = lifecycle_ctx.exit_code;
    return ok;
}

bool DefaultBootstrap(AppRunContext &ctx) {
    AppLifecycleContext lifecycle_ctx{};
    lifecycle_ctx.argc = ctx.argc;
    lifecycle_ctx.argv = ctx.argv;
    lifecycle_ctx.exit_code = ctx.exit_code;

    lifecycle_ctx.runtime = &g_runtime;
    const bool ok = AppLifecycleBootstrap(lifecycle_ctx);
    ctx.exit_code = lifecycle_ctx.exit_code;
    return ok;
}

void DefaultRun(AppRunContext &ctx) {
    AppLifecycleContext lifecycle_ctx{};
    lifecycle_ctx.argc = ctx.argc;
    lifecycle_ctx.argv = ctx.argv;
    lifecycle_ctx.exit_code = ctx.exit_code;

    lifecycle_ctx.runtime = &g_runtime;
    AppLifecycleRun(lifecycle_ctx);
    ctx.exit_code = lifecycle_ctx.exit_code;
}

void DefaultTeardown(AppRunContext &ctx) {
    AppLifecycleContext lifecycle_ctx{};
    lifecycle_ctx.argc = ctx.argc;
    lifecycle_ctx.argv = ctx.argv;
    lifecycle_ctx.exit_code = ctx.exit_code;

    lifecycle_ctx.runtime = &g_runtime;
    AppLifecycleTeardown(lifecycle_ctx);
    ctx.exit_code = lifecycle_ctx.exit_code;
}

}  // namespace

const AppLifecycleOps &GetDefaultLifecycleOps() {
    static const AppLifecycleOps kOps{
        &DefaultInit,
        &DefaultBootstrap,
        &DefaultRun,
        &DefaultTeardown,
    };
    return kOps;
}

int RunOverlayApp(int argc, char *argv[], const AppLifecycleOps &ops) {
    AppRunContext ctx{};
    ctx.argc = argc;
    ctx.argv = argv;

    InitProcessLogger();
    LogInfo("application startup");

    if (!ops.Init || !ops.Bootstrap || !ops.Run || !ops.Teardown) {
        LogError("invalid lifecycle ops");
        ShutdownProcessLogger();
        return -1;
    }

    if (!ops.Init(ctx)) {
        LogError("lifecycle init failed, exit_code=%d", ctx.exit_code);
        ShutdownProcessLogger();
        return ctx.exit_code;
    }

    if (!ops.Bootstrap(ctx)) {
        LogError("lifecycle bootstrap failed, exit_code=%d", ctx.exit_code);
        ops.Teardown(ctx);
        ShutdownProcessLogger();
        return ctx.exit_code;
    }

    ops.Run(ctx);
    ops.Teardown(ctx);
    LogInfo("application exit, code=%d", ctx.exit_code);
    ShutdownProcessLogger();
    return ctx.exit_code;
}

int RunOverlayApp(int argc, char *argv[]) {
    return RunOverlayApp(argc, argv, GetDefaultLifecycleOps());
}

}  // namespace desktoper2D

