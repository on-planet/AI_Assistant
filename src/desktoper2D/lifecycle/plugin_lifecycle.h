#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace desktoper2D {
    enum class PluginModuleCategory {
        Behavior,
        Ocr,
        Asr,
        Generic,
    };

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

// 生命周期编排最小端口：避免阶段函数直接依赖全量 AppRuntime。
struct LifecycleStepContext {
    using LogFn = void (*)(void *user_data, const char *message);
    using SetPluginEnabledFn = void (*)(void *user_data, bool enabled);
    using SetOpsStatusFn = void (*)(void *user_data, const char *status);

    void *user_data = nullptr;
    LogFn log = nullptr;
    SetPluginEnabledFn set_plugin_enabled = nullptr;
    SetOpsStatusFn set_ops_status = nullptr;

    void Log(const char *message) const {
        if (log) {
            log(user_data, message);
        }
    }

    void SetPluginEnabled(const bool enabled) const {
        if (set_plugin_enabled) {
            set_plugin_enabled(user_data, enabled);
        }
    }

    void SetOpsStatus(const char *status) const {
        if (set_ops_status) {
            set_ops_status(user_data, status);
        }
    }
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

struct RoutingEvidenceCandidate {
    std::string label;
    float confidence = 0.0f;
};

struct RoutingEvidence {
    int schema_version = 1;
    std::string primary_label;
    float primary_confidence = 0.0f;
    float primary_structured_confidence = 0.0f;
    std::string secondary_label;
    float secondary_confidence = 0.0f;
    float secondary_structured_confidence = 0.0f;
    std::vector<RoutingEvidenceCandidate> secondary_top_candidates;
    float scene_confidence = 0.0f;
    float source_scene_weight = 0.0f;
    float source_ocr_weight = 0.0f;
    float source_context_weight = 0.0f;
};

struct PerceptionInput {
    int schema_version = 1;
    double time_sec = 0.0;

    // 兼容旧字段（建议新实现走 feature_packets）。
    float audio_level = 0.0f;
    float user_presence = 0.0f;

    // 兼容旧字符串路由提示；新实现优先走 routing 结构化证据。
    // 典型值：game / code / meeting / unknown
    std::string scene_label;
    std::string task_label;

    AudioFeaturePacket audio;
    VisionFeaturePacket vision;
    RuntimeStateFeaturePacket state;
    RoutingEvidence routing;
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

struct PluginRouteRule {
    std::string name;
    std::vector<std::string> scene_keywords;
    std::vector<std::string> task_keywords;
    std::vector<std::string> scene_negative_keywords;
    std::vector<std::string> task_negative_keywords;
    std::vector<std::string> primary_labels;
    std::vector<std::string> secondary_labels;
    std::vector<std::string> primary_negative_labels;
    std::vector<std::string> secondary_negative_labels;
    int top_k_limit = 0;
    int memory_hold_frames = 0;
    int switch_out_cooldown_frames = 0;
    float scene_weight = 1.5f;
    float task_weight = 1.0f;
    float structured_weight = 1.35f;
    float negative_weight = 1.25f;
    float primary_weight = 1.1f;
    float secondary_weight = 1.2f;
    float top_k_weight = 0.85f;
    float memory_weight = 0.2f;
    float min_primary_confidence = 0.0f;
    float min_secondary_confidence = 0.0f;
    float min_total_score = 0.0f;
    float opacity_bias = 0.0f;
};

struct PluginRouteConfig {
    std::string default_route = "unknown";
    int top_k_candidates = 3;
    int switch_hold_frames = 6;
    int switch_cooldown_frames = 18;
    float switch_score_margin = 0.15f;
    float activation_threshold = 0.25f;
    float memory_decay = 0.85f;
    float expert_gating_temperature = 1.0f;
    std::vector<PluginRouteRule> rules;
};

enum class PluginPostprocessStepType {
    Normalize,
    Tokenize,
    Argmax,
    Threshold,
};

struct PluginPostprocessStep {
    PluginPostprocessStepType type = PluginPostprocessStepType::Normalize;
    std::string name;
    float epsilon = 1e-6f;      // normalize
    std::string delimiter = " "; // tokenize
    float threshold = 0.5f;     // threshold
};

struct PluginPostprocessConfig {
    std::vector<PluginPostprocessStep> steps;
};

struct PluginArtifactSpec {
    struct TensorSchema {
        std::string input_name = "input";
        std::string output_name = "output";
        std::int64_t batch = 1;
        std::int64_t feature_dim = 8;
    } tensor_schema;

    struct ExpertModelSpec {
        std::string route_name;
        std::string onnx_path;
        float fusion_weight = 0.35f;
    };

    // 固化 AIPlugin 交付接口：.onnx + config.json
    std::string onnx_path;
    std::string config_path;

    // 原子绑定元数据：配置与模型必须匹配。
    std::string model_id;
    std::string model_version;
    std::string onnx_checksum_fnv1a64;

    // 执行后端策略（优先级）与最终命中后端。
    std::vector<std::string> backend_priority;
    std::string resolved_backend = "cpu";

    // 后处理流水线（配置驱动）
    PluginPostprocessConfig postprocess;

    // 可选：多模型协作（专家集）
    std::vector<std::string> extra_onnx_paths;
    std::vector<ExpertModelSpec> expert_models;
    // scene/task 路由表（配置驱动，避免硬编码）
    PluginRouteConfig route_config;

    // 插件级 worker 调度策略（可由 config.json 覆盖）
    struct WorkerTuning {
        int update_hz = 60;
        int frame_budget_ms = 1;
        int timeout_degrade_threshold = 8;
        std::vector<int> degrade_hz_steps = {120, 60, 30, 15, 10};
        int recover_after_consecutive_successes = 24;
        double avg_latency_budget_ms = 8.0;
        std::size_t latency_budget_window_size = 24;
        int disable_after_consecutive_failures = 24;
        int auto_recover_after_ms = 5000;
    } worker_tuning;
};

class IBehaviorPluginFactory {
public:
    virtual ~IBehaviorPluginFactory() = default;

    virtual std::unique_ptr<IBehaviorPlugin> CreateFromConfig(const std::string &config_path,
                                                              std::string *out_error,
                                                              PluginArtifactSpec *out_spec = nullptr) = 0;

    virtual std::unique_ptr<IBehaviorPlugin> CreateDefault() = 0;
};

struct PluginRouteDecisionSnapshot {
    std::string selected_route = "unknown";
    std::vector<std::string> ranked_routes;
    std::vector<float> ranked_scores;
};

struct PluginWorkerConfig;
PluginWorkerConfig BuildPluginWorkerConfig(const PluginArtifactSpec::WorkerTuning &tuning);

std::unique_ptr<IBehaviorPlugin> CreateDefaultBehaviorPlugin();
std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPlugin(const PluginArtifactSpec &spec);
std::unique_ptr<IBehaviorPlugin> CreateOnnxBehaviorPluginFromConfig(const std::string &config_path,
                                                                    std::string *out_error,
                                                                    PluginArtifactSpec *out_spec);

struct PluginWorkerConfig {
    int update_hz = 60;
    int frame_budget_ms = 1;
    int timeout_degrade_threshold = 8;

    // 调度/降级策略
    std::vector<int> degrade_hz_steps = {120, 60, 30, 15, 10};
    int recover_after_consecutive_successes = 24;
    double avg_latency_budget_ms = 8.0;
    std::size_t latency_budget_window_size = 24;

    // 自动禁用与恢复策略
    int disable_after_consecutive_failures = 24;
    int auto_recover_after_ms = 5000;
};

struct PluginWorkerStats {
    std::uint64_t total_update_count = 0;
    std::uint64_t success_count = 0;
    std::uint64_t timeout_count = 0;
    std::uint64_t exception_count = 0;
    std::uint64_t internal_error_count = 0;
    std::uint64_t disable_count = 0;
    std::uint64_t recover_count = 0;
    int current_update_hz = 60;
    bool auto_disabled = false;

    // observability
    std::string model_id = "unknown";
    std::string model_version = "0.0.0";
    std::string backend = "onnxruntime.cpu";
    int batch = 1;
    double last_latency_ms = 0.0;
    double avg_latency_ms = 0.0;
    double latency_p50_ms = 0.0;
    double latency_p95_ms = 0.0;
    double success_rate = 0.0;
    double timeout_rate = 0.0;
    std::string last_error_code = "OK";
    std::string last_error;

    // module tag
    std::string module_name;
    PluginModuleCategory module_category = PluginModuleCategory::Generic;
};

struct PluginWorkerImpl {
    std::atomic<bool> running{false};
    std::thread worker;
    std::mutex mtx;
    std::condition_variable cv;
    PluginManager *manager = nullptr;
    PluginWorkerConfig cfg{};
    std::deque<PerceptionInput> input_queue;
    BehaviorOutput latest_out{};
    std::uint64_t out_seq = 0;
    std::uint64_t consumed_seq = 0;
    bool next_run_scheduled = false;
    std::chrono::steady_clock::time_point next_run_tp{};

    std::uint64_t total_update_count = 0;
    std::uint64_t success_count = 0;
    std::uint64_t timeout_count = 0;
    std::uint64_t exception_count = 0;
    std::uint64_t internal_error_count = 0;
    std::uint64_t disable_count = 0;
    std::uint64_t recover_count = 0;
    int current_update_hz = 60;
    int consecutive_timeout_count = 0;
    int consecutive_success_count = 0;
    int consecutive_failures = 0;
    bool auto_disabled = false;
    std::chrono::steady_clock::time_point last_disable_tp{};
    std::string last_error;

    std::string model_id = "unknown";
    std::string model_version = "0.0.0";
    std::string backend = "onnxruntime.cpu";
    int batch = 1;
    double last_latency_ms = 0.0;
    std::vector<double> latency_ring_ms;
    std::size_t latency_ring_head = 0;
    std::size_t latency_ring_size = 0;
    double latency_ring_sum_ms = 0.0;

    int PickNextDegradeHz() const {
        int next_hz = current_update_hz;
        for (int hz : cfg.degrade_hz_steps) {
            if (hz < current_update_hz) {
                next_hz = hz;
                break;
            }
        }
        return std::max(1, next_hz);
    }

    int PickNextRecoverHz() const {
        int next_hz = current_update_hz;
        for (auto it = cfg.degrade_hz_steps.rbegin(); it != cfg.degrade_hz_steps.rend(); ++it) {
            if (*it > current_update_hz) {
                next_hz = *it;
                break;
            }
        }
        return std::max(1, next_hz);
    }

    void ResetLatencyRing() {
        latency_ring_ms.clear();
        latency_ring_head = 0;
        latency_ring_size = 0;
        latency_ring_sum_ms = 0.0;
    }

    void PushLatencyMs(double ms) {
        const std::size_t cap = std::max<std::size_t>(1, cfg.latency_budget_window_size);
        if (latency_ring_ms.size() != cap) {
            ResetLatencyRing();
            latency_ring_ms.assign(cap, 0.0);
        }

        const double v = std::max(0.0, ms);
        if (latency_ring_size < cap) {
            const std::size_t idx = (latency_ring_head + latency_ring_size) % cap;
            latency_ring_ms[idx] = v;
            latency_ring_sum_ms += v;
            ++latency_ring_size;
        } else {
            const std::size_t idx = latency_ring_head;
            latency_ring_sum_ms -= latency_ring_ms[idx];
            latency_ring_ms[idx] = v;
            latency_ring_sum_ms += v;
            latency_ring_head = (latency_ring_head + 1) % cap;
        }
    }

    double ComputeAverageLatencyMs() const {
        if (latency_ring_size == 0) {
            return 0.0;
        }
        return latency_ring_sum_ms / static_cast<double>(latency_ring_size);
    }

    std::vector<double> SnapshotLatency() const {
        std::vector<double> out;
        out.reserve(latency_ring_size);
        if (latency_ring_size == 0 || latency_ring_ms.empty()) {
            return out;
        }
        const std::size_t cap = latency_ring_ms.size();
        for (std::size_t i = 0; i < latency_ring_size; ++i) {
            out.push_back(latency_ring_ms[(latency_ring_head + i) % cap]);
        }
        return out;
    }
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

    void SubmitInput(PerceptionInput in);
    bool TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr);
    PluginWorkerStats GetStats() const;

private:
    std::unique_ptr<PluginWorkerImpl> impl_;
};


class IPluginModule {
public:
    virtual ~IPluginModule() = default;

    virtual const char *Name() const = 0;
    virtual PluginModuleCategory Category() const = 0;
    [[nodiscard]] virtual bool ProducesBehaviorOutput() const = 0;

    virtual PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                              const PluginHostCallbacks &host,
                              std::string *out_error) = 0;
    virtual PluginStatus Update(PerceptionInput in,
                                BehaviorOutput *out,
                                std::string *out_error) = 0;
    virtual void Shutdown() noexcept = 0;
    [[nodiscard]] virtual PluginWorkerStats GetStats() const = 0;
    [[nodiscard]] virtual PluginWorkerConfig GetWorkerConfig() const = 0;
    [[nodiscard]] virtual const PluginDescriptor &Descriptor() const = 0;
};

class BehaviorPluginModule final : public IPluginModule {
public:
    explicit BehaviorPluginModule(std::unique_ptr<IBehaviorPlugin> plugin,
                                  PluginWorkerConfig worker_cfg = {});

    const char *Name() const override;
    PluginModuleCategory Category() const override;
    [[nodiscard]] bool ProducesBehaviorOutput() const override;

    PluginStatus Init(const PluginRuntimeConfig &runtime_cfg,
                      const PluginHostCallbacks &host,
                      std::string *out_error) override;
    PluginStatus Update(PerceptionInput in,
                        BehaviorOutput *out,
                        std::string *out_error) override;
    void Shutdown() noexcept override;
    [[nodiscard]] PluginWorkerStats GetStats() const override;
    [[nodiscard]] PluginWorkerConfig GetWorkerConfig() const override;
    [[nodiscard]] const PluginDescriptor &Descriptor() const override;

private:
    PluginManager manager_;
    PluginWorker worker_;
    PluginWorkerConfig worker_cfg_{};
    bool ready_ = false;
    BehaviorOutput last_output_{};
};

class PluginWorkerManager {
public:
    PluginWorkerManager();
    ~PluginWorkerManager();

    PluginWorkerManager(const PluginWorkerManager &) = delete;
    PluginWorkerManager &operator=(const PluginWorkerManager &) = delete;

    bool RegisterModule(std::unique_ptr<IPluginModule> module);
    bool Init(const PluginRuntimeConfig &runtime_cfg,
              const PluginHostCallbacks &host,
              const PluginWorkerConfig &worker_cfg,
              std::string *out_error);
    void Shutdown() noexcept;

    void SubmitInput(PerceptionInput in);
    bool TryConsumeLatestBehaviorOutput(BehaviorOutput &out, std::uint64_t *out_seq = nullptr);
    PluginWorkerStats GetStats() const;
    [[nodiscard]] const PluginDescriptor &Descriptor() const;
    bool IsRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace desktoper2D
