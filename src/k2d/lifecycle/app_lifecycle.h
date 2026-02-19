#pragma once

namespace k2d {

struct AppLifecycleContext {
    int argc = 0;
    char **argv = nullptr;
    int exit_code = 0;
};

bool AppLifecycleInit(AppLifecycleContext &ctx);
bool AppLifecycleBootstrap(AppLifecycleContext &ctx);
void AppLifecycleRun(AppLifecycleContext &ctx);
void AppLifecycleTeardown(AppLifecycleContext &ctx);

}  // namespace k2d
