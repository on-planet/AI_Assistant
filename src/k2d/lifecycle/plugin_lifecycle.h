#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace k2d {

struct PluginRuntimeConfig {
    bool show_debug_stats = true;
    bool manual_param_mode = false;
    bool click_through = false;
    float window_opacity = 1.0f;
};

struct PluginHostCallbacks {
    void (*log)(void *user_data, const char *message) = nullptr;
    void *user_data = nullptr;
};

struct PerceptionInput {
    double time_sec = 0.0;
    float audio_level = 0.0f;
    float user_presence = 0.0f;
};

struct BehaviorOutput {
    float param_targets[16]{};
    float param_weights[16]{};
    bool trigger_blink = false;
    bool trigger_idle_shift = false;
};

enum class PluginStatus {
    Ok = 0,
    InvalidArg,
    Timeout,
    InternalError,
};

struct PluginDescriptor {
    const char *name = "unknown";
    const char *version = "0.0.0";
    const char *capabilities = "";
};

class IBehaviorPlugin {
public:
    virtual ~IBehaviorPlugin() = default;

    virtual PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                              const PluginHostCallbacks &host,
                              PluginDescriptor *out_desc,
                              std::string *out_error) = 0;

    virtual PluginStatus Update(const PerceptionInput &in,
                                BehaviorOutput &out,
                                std::string *out_error) = 0;

    virtual void Destroy() noexcept = 0;
};

class PluginManager {
public:
    void SetPlugin(std::unique_ptr<IBehaviorPlugin> plugin);

    PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      std::string *out_error);

    PluginStatus Update(const PerceptionInput &in,
                        BehaviorOutput &out,
                        std::string *out_error);

    void Destroy() noexcept;

    [[nodiscard]] bool IsReady() const noexcept { return initialized_; }
    [[nodiscard]] const PluginDescriptor &Descriptor() const noexcept { return descriptor_; }

private:
    std::unique_ptr<IBehaviorPlugin> plugin_;
    PluginDescriptor descriptor_{};
    bool initialized_ = false;
};

std::unique_ptr<IBehaviorPlugin> CreateDefaultBehaviorPlugin();

struct PluginWorkerConfig {
    int update_hz = 60;
    int frame_budget_ms = 1;
    int timeout_degrade_threshold = 8;
};

struct PluginWorkerStats {
    std::uint64_t timeout_count = 0;
    std::uint64_t exception_count = 0;
    int current_update_hz = 60;
    std::string last_error;
};

class PluginWorker {
public:
    PluginWorker();
    ~PluginWorker();

    PluginWorker(const PluginWorker &) = delete;
    PluginWorker &operator=(const PluginWorker &) = delete;
    PluginWorker(PluginWorker &&other) noexcept;
    PluginWorker &operator=(PluginWorker &&other) noexcept;

    bool Start(PluginManager *manager, PluginWorkerConfig cfg, std::string *out_error);
    void Stop() noexcept;

    void SubmitInput(const PerceptionInput &in);
    bool TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr);
    PluginWorkerStats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace k2d
