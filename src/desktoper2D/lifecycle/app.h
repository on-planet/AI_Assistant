#pragma once

namespace desktoper2D {

struct AppRunContext {
    int argc = 0;
    char **argv = nullptr;
    int exit_code = 0;
};

struct AppLifecycleOps {
    bool (*Init)(AppRunContext &ctx) = nullptr;
    bool (*Bootstrap)(AppRunContext &ctx) = nullptr;
    void (*Run)(AppRunContext &ctx) = nullptr;
    void (*Teardown)(AppRunContext &ctx) = nullptr;
};

int RunOverlayApp(int argc, char *argv[], const AppLifecycleOps &ops);
int RunOverlayApp(int argc, char *argv[]);

const AppLifecycleOps &GetDefaultLifecycleOps();

}  // namespace desktoper2D
