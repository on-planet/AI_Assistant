#pragma once

#include "desktoper2D/controllers/app_bootstrap.h"

namespace desktoper2D {

struct AppRuntime;

void ApplyRuntimeConfig(AppRuntime &runtime, const AppRuntimeConfig &cfg);

}  // namespace desktoper2D
