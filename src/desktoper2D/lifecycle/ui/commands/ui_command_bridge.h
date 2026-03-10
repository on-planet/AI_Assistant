#pragma once

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

void EnqueueUiCommand(AppRuntime &runtime, const UiCommand &cmd);

struct UiCommandBridge {
    using EnqueueFn = void (*)(void *ctx, const UiCommand &cmd);
    using SetOpsStatusFn = void (*)(void *ctx, const std::string &status);

    void *ctx = nullptr;
    EnqueueFn enqueue = nullptr;
    SetOpsStatusFn set_ops_status = nullptr;

    void Enqueue(const UiCommand &cmd) const {
        if (enqueue) {
            enqueue(ctx, cmd);
        }
    }

    void SetOpsStatus(const std::string &status) const {
        if (set_ops_status) {
            set_ops_status(ctx, status);
        }
    }
};

UiCommandBridge BuildUiCommandBridge(AppRuntime &runtime);
void ExecuteUiCommand(AppRuntime &runtime, const UiCommand &cmd);
void ConsumeUiCommandQueue(AppRuntime &runtime);

}  // namespace desktoper2D
