#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <algorithm>
#include <utility>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

#include "desktoper2D/core/async_logger.h"

namespace desktoper2D {

void PluginManager::SetPlugin(std::unique_ptr<IBehaviorPlugin> plugin) {
    if (initialized_) {
        Destroy();
    }
    plugin_ = std::move(plugin);
    descriptor_ = PluginDescriptor{};
}

PluginStatus PluginManager::Init(const PluginRuntimeConfig &runtime_cfg,
                                 const PluginHostCallbacks &host,
                                 std::string *out_error) {
    if (!plugin_) {
        if (out_error) *out_error = "PluginManager Init failed: no plugin registered";
        return PluginStatus::InvalidArg;
    }

    descriptor_ = PluginDescriptor{};
    try {
        const PluginStatus st = plugin_->Init(runtime_cfg, host, &descriptor_, out_error);
        initialized_ = (st == PluginStatus::Ok);
        return st;
    } catch (const std::exception &e) {
        initialized_ = false;
        if (out_error) *out_error = std::string("PluginManager Init exception: ") + e.what();
        return PluginStatus::InternalError;
    } catch (...) {
        initialized_ = false;
        if (out_error) *out_error = "PluginManager Init exception: unknown";
        return PluginStatus::InternalError;
    }
}

PluginStatus PluginManager::Update(const PerceptionInput &in,
                                   BehaviorOutput &out,
                                   std::string *out_error) {
    if (!plugin_ || !initialized_) {
        if (out_error) *out_error = "PluginManager Update failed: plugin not ready";
        return PluginStatus::InternalError;
    }

    try {
        return plugin_->Update(in, out, out_error);
    } catch (const std::exception &e) {
        if (out_error) *out_error = std::string("PluginManager Update exception: ") + e.what();
        return PluginStatus::InternalError;
    } catch (...) {
        if (out_error) *out_error = "PluginManager Update exception: unknown";
        return PluginStatus::InternalError;
    }
}

void PluginManager::Destroy() noexcept {
    if (plugin_) {
        plugin_->Destroy();
    }
    initialized_ = false;
    descriptor_ = PluginDescriptor{};
}

PluginWorkerConfig BuildPluginWorkerConfig(const PluginArtifactSpec::WorkerTuning &tuning) {
    PluginWorkerConfig cfg{};
    cfg.update_hz = tuning.update_hz;
    cfg.frame_budget_ms = tuning.frame_budget_ms;
    cfg.timeout_degrade_threshold = tuning.timeout_degrade_threshold;
    cfg.degrade_hz_steps = tuning.degrade_hz_steps;
    cfg.recover_after_consecutive_successes = tuning.recover_after_consecutive_successes;
    cfg.avg_latency_budget_ms = tuning.avg_latency_budget_ms;
    cfg.latency_budget_window_size = tuning.latency_budget_window_size;
    cfg.disable_after_consecutive_failures = tuning.disable_after_consecutive_failures;
    cfg.auto_recover_after_ms = tuning.auto_recover_after_ms;
    return cfg;
}


PluginWorker::PluginWorker() = default;

PluginWorker::~PluginWorker() {
    Stop();
}

PluginWorker::PluginWorker(PluginWorker &&other) noexcept = default;

PluginWorker &PluginWorker::operator=(PluginWorker &&other) noexcept {
    if (this != &other) {
        Stop();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool PluginWorker::Start(PluginManager *manager, PluginWorkerConfig cfg, std::string *out_error) {
    if (!manager) {
        if (out_error) *out_error = "PluginWorker Start failed: manager is null";
        return false;
    }
    if (!manager->IsReady()) {
        if (out_error) *out_error = "PluginWorker Start failed: plugin manager not initialized";
        return false;
    }
    if (impl_ && impl_->running.load()) {
        if (out_error) *out_error = "PluginWorker Start failed: already running";
        return false;
    }

    if (!impl_) {
        impl_ = std::make_unique<PluginWorkerImpl>();
    }

    impl_->manager = manager;
    impl_->cfg = cfg;
    impl_->cfg.update_hz = std::max(1, impl_->cfg.update_hz);
    impl_->cfg.frame_budget_ms = std::max(1, impl_->cfg.frame_budget_ms);
    impl_->cfg.timeout_degrade_threshold = std::max(1, impl_->cfg.timeout_degrade_threshold);
    impl_->cfg.disable_after_consecutive_failures = std::max(1, impl_->cfg.disable_after_consecutive_failures);
    impl_->cfg.auto_recover_after_ms = std::max(100, impl_->cfg.auto_recover_after_ms);
    impl_->cfg.recover_after_consecutive_successes = std::max(1, impl_->cfg.recover_after_consecutive_successes);
    impl_->cfg.avg_latency_budget_ms = std::max(0.1, impl_->cfg.avg_latency_budget_ms);
    impl_->cfg.latency_budget_window_size = std::max<std::size_t>(4, impl_->cfg.latency_budget_window_size);
    if (impl_->cfg.degrade_hz_steps.empty()) {
        impl_->cfg.degrade_hz_steps = {impl_->cfg.update_hz, 60, 30, 15, 10};
    }
    std::sort(impl_->cfg.degrade_hz_steps.begin(), impl_->cfg.degrade_hz_steps.end(), std::greater<int>());
    impl_->cfg.degrade_hz_steps.erase(std::remove_if(impl_->cfg.degrade_hz_steps.begin(),
                                                     impl_->cfg.degrade_hz_steps.end(),
                                                     [](int hz) { return hz <= 0; }),
                                      impl_->cfg.degrade_hz_steps.end());
    if (impl_->cfg.degrade_hz_steps.empty() || impl_->cfg.degrade_hz_steps.front() != impl_->cfg.update_hz) {
        impl_->cfg.degrade_hz_steps.insert(impl_->cfg.degrade_hz_steps.begin(), impl_->cfg.update_hz);
        std::sort(impl_->cfg.degrade_hz_steps.begin(), impl_->cfg.degrade_hz_steps.end(), std::greater<int>());
    }
    impl_->current_update_hz = impl_->cfg.update_hz;
    impl_->consecutive_timeout_count = 0;
    impl_->consecutive_success_count = 0;
    impl_->consecutive_failures = 0;
    impl_->auto_disabled = false;
    impl_->input_queue.clear();
    impl_->latest_out = BehaviorOutput{};
    impl_->out_seq = 0;
    impl_->consumed_seq = 0;
    impl_->next_run_scheduled = false;
    impl_->next_run_tp = std::chrono::steady_clock::time_point{};
    impl_->total_update_count = 0;
    impl_->success_count = 0;
    impl_->timeout_count = 0;
    impl_->exception_count = 0;
    impl_->internal_error_count = 0;
    impl_->disable_count = 0;
    impl_->recover_count = 0;
    impl_->last_latency_ms = 0.0;
    impl_->ResetLatencyRing();
    impl_->last_error.clear();
    const PluginDescriptor &desc = manager->Descriptor();
    impl_->model_id = desc.name ? desc.name : "unknown";
    impl_->model_version = desc.version ? desc.version : "0.0.0";
    impl_->backend = "onnxruntime.cpu";
    impl_->batch = 1;
    impl_->running.store(true);

    impl_->worker = std::thread([this]() {
        while (impl_->running.load()) {
            PerceptionInput in{};
            auto t0 = std::chrono::steady_clock::time_point{};

            {
                std::unique_lock<std::mutex> lock(impl_->mtx);
                while (impl_->running.load()) {
                    if (impl_->input_queue.empty()) {
                        impl_->next_run_scheduled = false;
                        impl_->cv.wait(lock, [this]() { return !impl_->running.load() || !impl_->input_queue.empty(); });
                        continue;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    if (impl_->auto_disabled) {
                        const auto recover_tp =
                            impl_->last_disable_tp + std::chrono::milliseconds(std::max(0, impl_->cfg.auto_recover_after_ms));
                        if (now < recover_tp) {
                            impl_->cv.wait_until(lock, recover_tp, [this]() { return !impl_->running.load(); });
                            continue;
                        }

                        impl_->auto_disabled = false;
                        impl_->consecutive_failures = 0;
                        ++impl_->recover_count;
                        impl_->last_error = "plugin auto recovered";
                        impl_->next_run_scheduled = false;
                    }
                    if (!impl_->next_run_scheduled) {
                        impl_->next_run_tp = now;
                        impl_->next_run_scheduled = true;
                    }

                    if (now < impl_->next_run_tp) {
                        impl_->cv.wait_until(lock, impl_->next_run_tp, [this]() { return !impl_->running.load(); });
                        continue;
                    }

                    in = std::move(impl_->input_queue.front());
                    impl_->input_queue.pop_front();
                    t0 = std::chrono::steady_clock::now();
                    break;
                }
            }

            if (!impl_->running.load()) {
                break;
            }

            BehaviorOutput out{};
            std::string err;
            PluginStatus st = PluginStatus::InternalError;
            bool caught_exception = false;
            try {
                st = impl_->manager->Update(in, out, &err);
            } catch (const std::exception &e) {
                err = std::string("PluginWorker Update exception: ") + e.what();
                st = PluginStatus::InternalError;
                caught_exception = true;
            } catch (...) {
                err = "PluginWorker Update exception: unknown";
                st = PluginStatus::InternalError;
                caught_exception = true;
            }

            const double elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count());

            {
                std::lock_guard<std::mutex> lock(impl_->mtx);
                ++impl_->total_update_count;
                impl_->last_latency_ms = elapsed_ms;
                impl_->PushLatencyMs(elapsed_ms);
                const double avg_latency_ms = impl_->ComputeAverageLatencyMs();
                if (st == PluginStatus::Ok) {
                    impl_->latest_out = out;
                    ++impl_->out_seq;
                    ++impl_->success_count;
                    ++impl_->consecutive_success_count;
                    impl_->consecutive_timeout_count = 0;
                    impl_->consecutive_failures = 0;
                    impl_->auto_disabled = false;
                    impl_->last_error.clear();

                    if (avg_latency_ms > impl_->cfg.avg_latency_budget_ms) {
                        const int next_hz = impl_->PickNextDegradeHz();
                        if (next_hz != impl_->current_update_hz) {
                            impl_->current_update_hz = next_hz;
                            impl_->consecutive_success_count = 0;
                            LogInfo("PluginWorker degraded update_hz to %d due to avg latency budget %.2fms exceeded (avg=%.2fms)",
                                    next_hz,
                                    impl_->cfg.avg_latency_budget_ms,
                                    avg_latency_ms);
                        }
                    } else if (impl_->consecutive_success_count >= impl_->cfg.recover_after_consecutive_successes) {
                        const int next_hz = impl_->PickNextRecoverHz();
                        if (next_hz != impl_->current_update_hz) {
                            impl_->current_update_hz = next_hz;
                            LogInfo("PluginWorker recovered update_hz to %d after %d consecutive successes",
                                    next_hz,
                                    impl_->consecutive_success_count);
                        }
                        impl_->consecutive_success_count = 0;
                    }
                } else {
                    ++impl_->consecutive_failures;
                    impl_->consecutive_success_count = 0;
                    if (st == PluginStatus::Timeout) {
                        ++impl_->timeout_count;
                        ++impl_->consecutive_timeout_count;

                        if (impl_->consecutive_timeout_count >= impl_->cfg.timeout_degrade_threshold) {
                            const int next_hz = impl_->PickNextDegradeHz();
                            if (next_hz != impl_->current_update_hz) {
                                impl_->current_update_hz = next_hz;
                                LogInfo("PluginWorker degraded update_hz to %d due to consecutive timeouts", next_hz);
                            }
                            impl_->consecutive_timeout_count = 0;
                        }
                    } else {
                        impl_->consecutive_timeout_count = 0;
                        ++impl_->internal_error_count;
                    }

                    if (caught_exception) {
                        ++impl_->exception_count;
                    }
                    if (!err.empty()) {
                        impl_->last_error = err;
                    }

                    if (!impl_->auto_disabled && impl_->consecutive_failures >= impl_->cfg.disable_after_consecutive_failures) {
                        impl_->auto_disabled = true;
                        impl_->last_disable_tp = std::chrono::steady_clock::now();
                        ++impl_->disable_count;
                        impl_->last_error = "plugin auto disabled due to consecutive failures";
                    }
                }

                const int next_hz = std::max(1, impl_->current_update_hz);
                if (impl_->input_queue.empty()) {
                    impl_->next_run_tp = std::chrono::steady_clock::time_point{};
                    impl_->next_run_scheduled = false;
                } else {
                    impl_->next_run_tp = t0 + std::chrono::milliseconds(std::max(1, 1000 / next_hz));
                    impl_->next_run_scheduled = true;
                }
            }
        }
    });

    return true;
}

void PluginWorker::Stop() noexcept {
    if (!impl_) return;
    impl_->running.store(false);
    impl_->cv.notify_one();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}

void PluginWorker::SubmitInput(PerceptionInput in) {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mtx);
        impl_->input_queue.push_back(std::move(in));
    }
    impl_->cv.notify_one();
}

bool PluginWorker::TryConsumeLatestOutput(BehaviorOutput &out, std::uint64_t *out_seq) {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if (impl_->out_seq == impl_->consumed_seq) {
        return false;
    }
    out = impl_->latest_out;
    impl_->consumed_seq = impl_->out_seq;
    if (out_seq) *out_seq = impl_->out_seq;
    return true;
}

PluginWorkerStats PluginWorker::GetStats() const {
    if (!impl_) {
        return PluginWorkerStats{};
    }
    std::lock_guard<std::mutex> lock(impl_->mtx);
    PluginWorkerStats s{};
    s.total_update_count = impl_->total_update_count;
    s.success_count = impl_->success_count;
    s.timeout_count = impl_->timeout_count;
    s.exception_count = impl_->exception_count;
    s.internal_error_count = impl_->internal_error_count;
    s.disable_count = impl_->disable_count;
    s.recover_count = impl_->recover_count;
    s.current_update_hz = impl_->current_update_hz;
    s.auto_disabled = impl_->auto_disabled;
    s.model_id = impl_->model_id;
    s.model_version = impl_->model_version;
    s.backend = impl_->backend;
    s.batch = impl_->batch;
    s.last_latency_ms = impl_->last_latency_ms;
    s.avg_latency_ms = impl_->ComputeAverageLatencyMs();
    const std::vector<double> latency_samples = impl_->SnapshotLatency();
    if (!latency_samples.empty()) {
        std::vector<double> sorted = latency_samples;
        std::sort(sorted.begin(), sorted.end());
        const std::size_t last = sorted.size() - 1;
        const std::size_t p50_idx = static_cast<std::size_t>(std::floor(0.50 * static_cast<double>(last)));
        const std::size_t p95_idx = static_cast<std::size_t>(std::floor(0.95 * static_cast<double>(last)));
        s.latency_p50_ms = sorted[std::min(p50_idx, last)];
        s.latency_p95_ms = sorted[std::min(p95_idx, last)];
    }
    s.success_rate = impl_->total_update_count > 0
                         ? static_cast<double>(impl_->success_count) / static_cast<double>(impl_->total_update_count)
                         : 0.0;
    s.timeout_rate = impl_->total_update_count > 0
                         ? static_cast<double>(impl_->timeout_count) / static_cast<double>(impl_->total_update_count)
                         : 0.0;
    s.last_error = impl_->last_error;
    return s;
}

BehaviorPluginModule::BehaviorPluginModule(std::unique_ptr<IBehaviorPlugin> plugin,
                                           PluginWorkerConfig worker_cfg)
    : worker_cfg_(std::move(worker_cfg)) {
    manager_.SetPlugin(std::move(plugin));
}

const char *BehaviorPluginModule::Name() const {
    return "behavior_plugin";
}

PluginModuleCategory BehaviorPluginModule::Category() const {
    return PluginModuleCategory::Behavior;
}

bool BehaviorPluginModule::ProducesBehaviorOutput() const {
    return true;
}

PluginStatus BehaviorPluginModule::Init(const PluginRuntimeConfig &runtime_cfg,
                                        const PluginHostCallbacks &host,
                                        std::string *out_error) {
    const PluginStatus st = manager_.Init(runtime_cfg, host, out_error);
    if (st != PluginStatus::Ok) {
        ready_ = false;
        return st;
    }

    if (!worker_.Start(&manager_, worker_cfg_, out_error)) {
        manager_.Destroy();
        ready_ = false;
        return PluginStatus::InternalError;
    }

    ready_ = true;
    return PluginStatus::Ok;
}

PluginStatus BehaviorPluginModule::Update(PerceptionInput in,
                                          BehaviorOutput *out,
                                          std::string *out_error) {
    if (!ready_) {
        if (out_error) *out_error = "behavior module not ready";
        return PluginStatus::InternalError;
    }

    worker_.SubmitInput(std::move(in));
    BehaviorOutput next{};
    if (worker_.TryConsumeLatestOutput(next, nullptr)) {
        last_output_ = next;
    }
    if (out) {
        *out = last_output_;
    }
    return PluginStatus::Ok;
}

void BehaviorPluginModule::Shutdown() noexcept {
    worker_.Stop();
    manager_.Destroy();
    ready_ = false;
}

PluginWorkerStats BehaviorPluginModule::GetStats() const {
    PluginWorkerStats stats = worker_.GetStats();
    stats.module_name = Name();
    stats.module_category = Category();
    return stats;
}

PluginWorkerConfig BehaviorPluginModule::GetWorkerConfig() const {
    return worker_cfg_;
}

const PluginDescriptor &BehaviorPluginModule::Descriptor() const {
    return manager_.Descriptor();
}

struct PluginWorkerManager::Impl {
    struct ModuleEntry {
        std::unique_ptr<IPluginModule> module;
        BehaviorOutput last_behavior;
        std::uint64_t last_behavior_seq = 0;
    };

    std::vector<ModuleEntry> modules;
    bool running = false;
    std::uint64_t behavior_seq = 0;
    std::uint64_t consumed_behavior_seq = 0;
    BehaviorOutput latest_behavior;

    void Register(std::unique_ptr<IPluginModule> module) {
        modules.push_back({std::move(module), {}, 0});
    }

    bool Init(const PluginRuntimeConfig &runtime_cfg,
              const PluginHostCallbacks &host,
              const PluginWorkerConfig &worker_cfg,
              std::string *out_error) {
        (void)worker_cfg;
        running = false;
        for (auto &entry : modules) {
            if (entry.module->Init(runtime_cfg, host, out_error) != PluginStatus::Ok) {
                return false;
            }
        }
        behavior_seq = 0;
        consumed_behavior_seq = 0;
        latest_behavior = BehaviorOutput{};
        running = true;
        return true;
    }

    void Shutdown() noexcept {
        for (auto &entry : modules) {
            entry.module->Shutdown();
        }
        running = false;
    }

    void SubmitInput(PerceptionInput in) {
        for (std::size_t i = 0; i < modules.size(); ++i) {
            auto &entry = modules[i];
            BehaviorOutput scratch{};
            PerceptionInput module_input = (i + 1 == modules.size()) ? std::move(in) : in;
            entry.module->Update(std::move(module_input), &scratch, nullptr);
            if (entry.module->ProducesBehaviorOutput()) {
                entry.last_behavior = scratch;
                entry.last_behavior_seq = ++behavior_seq;
                latest_behavior = scratch;
            }
        }
    }

    bool TryConsumeLatestBehaviorOutput(BehaviorOutput &out, std::uint64_t *out_seq) {
        if (behavior_seq == consumed_behavior_seq) {
            return false;
        }
        out = latest_behavior;
        consumed_behavior_seq = behavior_seq;
        if (out_seq) *out_seq = behavior_seq;
        return true;
    }

    PluginWorkerStats AggregateStats() const {
        PluginWorkerStats stats{};
        for (const auto &entry : modules) {
            const PluginWorkerStats module_stats = entry.module->GetStats();
            stats.total_update_count += module_stats.total_update_count;
            stats.success_count += module_stats.success_count;
            stats.timeout_count += module_stats.timeout_count;
            stats.exception_count += module_stats.exception_count;
            stats.internal_error_count += module_stats.internal_error_count;
            stats.disable_count += module_stats.disable_count;
            stats.recover_count += module_stats.recover_count;
        }
        return stats;
    }
};

PluginWorkerManager::PluginWorkerManager()
    : impl_(std::make_unique<Impl>()) {}

PluginWorkerManager::~PluginWorkerManager() = default;

bool PluginWorkerManager::RegisterModule(std::unique_ptr<IPluginModule> module) {
    if (!impl_) return false;
    impl_->Register(std::move(module));
    return true;
}

bool PluginWorkerManager::Init(const PluginRuntimeConfig &runtime_cfg,
                               const PluginHostCallbacks &host,
                               const PluginWorkerConfig &worker_cfg,
                               std::string *out_error) {
    if (!impl_) return false;
    return impl_->Init(runtime_cfg, host, worker_cfg, out_error);
}

void PluginWorkerManager::Shutdown() noexcept {
    if (impl_) {
        impl_->Shutdown();
    }
}

void PluginWorkerManager::SubmitInput(PerceptionInput in) {
    if (impl_) {
        impl_->SubmitInput(std::move(in));
    }
}

bool PluginWorkerManager::TryConsumeLatestBehaviorOutput(BehaviorOutput &out, std::uint64_t *out_seq) {
    if (!impl_) return false;
    return impl_->TryConsumeLatestBehaviorOutput(out, out_seq);
}

PluginWorkerStats PluginWorkerManager::GetStats() const {
    if (!impl_) return {};
    return impl_->AggregateStats();
}

const PluginDescriptor &PluginWorkerManager::Descriptor() const {
    static PluginDescriptor default_desc{};
    return default_desc;
}

bool PluginWorkerManager::IsRunning() const noexcept {
    return impl_ && impl_->running;
}

}  // namespace desktoper2D
