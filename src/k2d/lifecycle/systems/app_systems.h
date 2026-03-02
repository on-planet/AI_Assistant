#pragma once

namespace k2d {

struct AppRuntime;

// Systems 层：集中放业务系统更新入口（可逐步迁移）
void TickAppSystems(AppRuntime &runtime, float dt);

}  // namespace k2d
