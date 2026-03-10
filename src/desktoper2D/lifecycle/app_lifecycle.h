#pragma once

namespace desktoper2D {

struct AppRuntime;

struct AppLifecycleContext {
    int argc = 0;
    char **argv = nullptr;
    int exit_code = 0;
    AppRuntime *runtime = nullptr;
};

bool AppLifecycleInit(AppLifecycleContext &ctx);
bool AppLifecycleBootstrap(AppLifecycleContext &ctx);
void AppLifecycleRun(AppLifecycleContext &ctx);
void AppLifecycleTeardown(AppLifecycleContext &ctx);

}  // namespace desktoper2D
