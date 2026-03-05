#pragma once

#include "k2d/controllers/app_bootstrap.h"

namespace k2d {

struct AppRuntime;

void ApplyRuntimeConfig(AppRuntime &runtime, const AppRuntimeConfig &cfg);

}  // namespace k2d
