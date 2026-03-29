#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace desktoper2D {
namespace {

void SafeHostLog(const PluginHostCallbacks &host, const char *msg) {
    if (host.log) {
        host.log(host.user_data, msg ? msg : "");
        return;
    }
    SDL_Log("[Plugin] %s", msg ? msg : "");
}

class DefaultBehaviorPlugin final : public IBehaviorPlugin {
public:
    PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        if (!out_desc) {
            if (out_error) *out_error = "Init failed: out_desc is null";
            return PluginStatus::InvalidArg;
        }

        host_ = host;
        initialized_ = true;
        elapsed_time_sec_ = 0.0;

        *out_desc = PluginDescriptor{
            .name = "default_behavior",
            .version = "0.1.0",
            .capabilities = "idle_opacity_pulse",
        };

        base_show_debug_stats_ = runtime_cfg.show_debug_stats;
        base_manual_param_mode_ = runtime_cfg.manual_param_mode;
        base_click_through_ = runtime_cfg.click_through;
        base_opacity_ = std::clamp(runtime_cfg.window_opacity, 0.05f, 1.0f);

        SafeHostLog(host_, "DefaultBehaviorPlugin initialized");
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &in,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        if (!initialized_) {
            if (out_error) *out_error = "Update failed: plugin not initialized";
            return PluginStatus::InternalError;
        }

        elapsed_time_sec_ += std::max(0.0f, 1.0f / 60.0f);

        out.ClearPreservingCapacity();
        out.param_targets.reserve(4);
        out.param_weights.reserve(4);
        const float pulse = 0.1f * SDL_sinf(static_cast<float>(elapsed_time_sec_) * 1.2f);
        out.param_targets["window.opacity"] = std::clamp(base_opacity_ + pulse, 0.05f, 1.0f);
        out.param_weights["window.opacity"] = 1.0f;

        out.param_targets["window.click_through"] = base_click_through_ ? 1.0f : 0.0f;
        out.param_weights["window.click_through"] = 1.0f;

        out.param_targets["runtime.show_debug_stats"] = base_show_debug_stats_ ? 1.0f : 0.0f;
        out.param_weights["runtime.show_debug_stats"] = 1.0f;

        out.param_targets["runtime.manual_param_mode"] = base_manual_param_mode_ ? 1.0f : 0.0f;
        out.param_weights["runtime.manual_param_mode"] = 1.0f;

        out.trigger_idle_shift = (static_cast<int>(elapsed_time_sec_) % 4) == 0;
        out.trigger_blink = (in.user_presence > 0.8f);

        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        if (!initialized_) {
            return;
        }
        initialized_ = false;
        SafeHostLog(host_, "DefaultBehaviorPlugin destroyed");
    }

private:
    PluginHostCallbacks host_{};
    bool initialized_ = false;
    bool base_show_debug_stats_ = true;
    bool base_manual_param_mode_ = false;
    bool base_click_through_ = false;
    float base_opacity_ = 1.0f;
    double elapsed_time_sec_ = 0.0;
};

}  // namespace

std::unique_ptr<IBehaviorPlugin> CreateDefaultBehaviorPlugin() {
    return std::make_unique<DefaultBehaviorPlugin>();
}

}  // namespace desktoper2D
