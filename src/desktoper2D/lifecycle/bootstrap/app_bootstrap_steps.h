#pragma once

#include "desktoper2D/lifecycle/app_lifecycle.h"

namespace desktoper2D {

bool AppLifecycleInitImpl(AppLifecycleContext &ctx);
bool AppLifecycleBootstrapImpl(AppLifecycleContext &ctx);
void AppLifecycleTeardownImpl(AppLifecycleContext &ctx);

}  // namespace desktoper2D
