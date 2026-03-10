#pragma once

#include <string>

#include "imgui.h"

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

enum class HealthState {
    Green,
    Yellow,
    Red,
};

enum class RuntimeModuleState {
    Ok,
    Degraded,
    Failed,
    Recovering,
};

enum class ErrorViewFilter {
    All = 0,
    NonOk = 1,
    Failed = 2,
    Degraded = 3,
};

struct RuntimeErrorRow {
    const char *label = "";
    const RuntimeErrorInfo *info = nullptr;
    RuntimeModuleState state = RuntimeModuleState::Ok;
    int recent_seq = 0;
};

}  // namespace desktoper2D
