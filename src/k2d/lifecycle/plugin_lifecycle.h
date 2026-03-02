#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

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

struct AudioFeaturePacket {
    // 协议版本，便于后续扩展。
    int schema_version = 1;
    // 归一化短时能量 [0,1]。
    float level = 0.0f;
    // 主频/音高置信值（Hz, <=0 表示不可用）。
    float pitch_hz = 0.0f;
    // 语音活动检测 [0,1]。
    float vad_prob = 0.0f;
};

struct VisionFeaturePacket {
    int schema_version = 1;
    // 用户在镜头中的存在置信度 [0,1]。
    float user_presence = 0.0f;
    // 注视中心点（归一化坐标，[-1,1]，0 表示中心）。
    float gaze_x = 0.0f;
    float gaze_y = 0.0f;
    // 头部姿态（角度制）。
    float head_yaw_deg = 0.0f;
    float head_pitch_deg = 0.0f;
    float head_roll_deg = 0.0f;
};

struct RuntimeStateFeaturePacket {
    int schema_version = 1;
    bool window_visible = true;
    bool click_through = false;
    bool manual_param_mode = false;
    bool show_debug_stats = true;
    float dt_sec = 0.0f;
};

struct PerceptionInput {
    int schema_version = 1;
    double time_sec = 0.0;

    // 兼容旧字段（建议新实现走 feature_packets）。
    float audio_level = 0.0f;
    float user_presence = 0.0f;

    // 专家路由提示：先粗分类，再转发给子专家。
    // 典型值：game / code / meeting / unknown
    std::string scene_label;
    std::string task_label;

    AudioFeaturePacket audio;
    VisionFeaturePacket vision;
    RuntimeStateFeaturePacket state;
};

struct BehaviorOutput {
    int schema_version = 1;

    // 行为输出 schema：
    // - key: 参数/控制 id（如 "ParamAngleX"、"window.opacity"）
    // - value: 目标值（target）
    // - weight: 融合权重（weight, 建议 [0,1]）
    std::unordered_map<std::string, float> param_targets;
    std::unordered_map<std::string, float> param_weights;

    // 事件触发（一次性脉冲语义）。
    bool trigger_blink = false;
    bool trigger_idle_shift = false;

    // 扩展事件/标签输出（可选，供上层订阅）。
    std::unordered_map<std::string, float> event_scores;
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

struct PluginArtifactSpec {
    // 固化 AIPlugin 交付接口：.onnx + config.json
    std::string onnx_path;
    std::string config_path;
    // 可选：多模型协作（专家集）
    std::vector<std::string> extra_onnx_paths;
};

std::unique_ptr<IBehaviorPlugin> CreateDefaultBehaviorPlugin();
std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPlugin(const PluginArtifactSpec &spec);
std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPluginFromConfig(const std::string &config_path,
                                                                    std::string *out_error = nullptr);

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
