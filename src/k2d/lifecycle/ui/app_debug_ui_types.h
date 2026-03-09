#pragma once

#include <string>

#include "imgui.h"

#include "k2d/lifecycle/state/app_runtime_state.h"

namespace k2d {

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

}  // namespace k2d
