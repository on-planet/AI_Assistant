#include "desktoper2D/lifecycle/systems/runtime_command_executor.h"

namespace desktoper2D {

void PushRuntimeEvent(AppRuntime &runtime, const RuntimeEvent &event) {
    auto &bus = runtime.command_bus;
    if (bus.runtime_event_queue_capacity == 0) {
        return;
    }
    if (bus.runtime_event_queue.size() >= bus.runtime_event_queue_capacity) {
        bus.runtime_event_queue.pop_front();
    }
    bus.runtime_event_queue.push_back(event);
}

}  // namespace desktoper2D
