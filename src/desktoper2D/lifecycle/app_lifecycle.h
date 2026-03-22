#pragma once

namespace desktoper2D {

struct AppRuntime;

struct AppLifecyclePhaseCapabilityContext {
    AppRuntime *runtime = nullptr;
    int *exit_code = nullptr;

    AppRuntime *RequireRuntime() const {
        if (runtime) {
            return runtime;
        }
        if (exit_code) {
            *exit_code = 1;
        }
        return nullptr;
    }

    void SetExitCode(const int code) const {
        if (exit_code) {
            *exit_code = code;
        }
    }
};

struct AppLifecycleInitCapabilityContext {
    int argc = 0;
    char **argv = nullptr;
    AppRuntime *runtime = nullptr;
    int *exit_code = nullptr;

    AppRuntime *RequireRuntime() const {
        if (runtime) {
            return runtime;
        }
        if (exit_code) {
            *exit_code = 1;
        }
        return nullptr;
    }

    void SetExitCode(const int code) const {
        if (exit_code) {
            *exit_code = code;
        }
    }
};

struct AppLifecycleContext {
    int argc = 0;
    char **argv = nullptr;
    int exit_code = 0;
    AppRuntime *runtime = nullptr;

    [[nodiscard]] AppLifecycleInitCapabilityContext InitCapability() {
        return AppLifecycleInitCapabilityContext{
            .argc = argc,
            .argv = argv,
            .runtime = runtime,
            .exit_code = &exit_code,
        };
    }

    [[nodiscard]] AppLifecyclePhaseCapabilityContext BootstrapCapability() {
        return AppLifecyclePhaseCapabilityContext{
            .runtime = runtime,
            .exit_code = &exit_code,
        };
    }

    [[nodiscard]] AppLifecyclePhaseCapabilityContext RunCapability() {
        return AppLifecyclePhaseCapabilityContext{
            .runtime = runtime,
            .exit_code = &exit_code,
        };
    }

    [[nodiscard]] AppLifecyclePhaseCapabilityContext TeardownCapability() {
        return AppLifecyclePhaseCapabilityContext{
            .runtime = runtime,
            .exit_code = &exit_code,
        };
    }
};

bool AppLifecycleInit(AppLifecycleContext &ctx);
bool AppLifecycleBootstrap(AppLifecycleContext &ctx);
void AppLifecycleRun(AppLifecycleContext &ctx);
void AppLifecycleTeardown(AppLifecycleContext &ctx);

}  // namespace desktoper2D
