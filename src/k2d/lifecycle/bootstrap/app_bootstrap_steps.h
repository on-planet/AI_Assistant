#pragma once

#include "k2d/lifecycle/app_lifecycle.h"

namespace k2d {

bool AppLifecycleInitImpl(AppLifecycleContext &ctx);
bool AppLifecycleBootstrapImpl(AppLifecycleContext &ctx);
void AppLifecycleTeardownImpl(AppLifecycleContext &ctx);

}  // namespace k2d
