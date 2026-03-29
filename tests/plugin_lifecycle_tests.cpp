#include "desktoper2D/lifecycle/plugin_lifecycle.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

using desktoper2D::BehaviorOutput;
using desktoper2D::IBehaviorPlugin;
using desktoper2D::PerceptionInput;
using desktoper2D::PluginDescriptor;
using desktoper2D::PluginHostCallbacks;
using desktoper2D::PluginManager;
using desktoper2D::PluginRuntimeConfig;
using desktoper2D::PluginStatus;

struct Assert {
    static bool True(bool cond, const char *msg) {
        if (!cond) {
            std::cerr << "[FAIL] " << msg << "\n";
            return false;
        }
        return true;
    }

    static bool Eq(PluginStatus a, PluginStatus b, const char *msg) {
        if (a != b) {
            std::cerr << "[FAIL] " << msg << " (actual=" << static_cast<int>(a)
                      << ", expect=" << static_cast<int>(b) << ")\n";
            return false;
        }
        return true;
    }

    static bool Contains(const std::string &s, const char *needle, const char *msg) {
        if (s.find(needle) == std::string::npos) {
            std::cerr << "[FAIL] " << msg << " (text='" << s << "')\n";
            return false;
        }
        return true;
    }
};

class StubPlugin final : public IBehaviorPlugin {
public:
    enum class Mode {
        Success,
        InitFail,
        UpdateFail,
        UpdateTimeout,
        InitThrow,
        UpdateThrow,
    };

    explicit StubPlugin(Mode m) : mode_(m) {}

    PluginStatus Init(const PluginRuntimeConfig &,
                      const PluginHostCallbacks &,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        ++init_called_;
        if (mode_ == Mode::InitThrow) {
            throw std::runtime_error("init throw");
        }
        if (!out_desc) {
            if (out_error) *out_error = "out_desc null";
            return PluginStatus::InvalidArg;
        }
        *out_desc = PluginDescriptor{"stub", "1.0.0", "test"};
        if (mode_ == Mode::InitFail) {
            if (out_error) *out_error = "init fail";
            return PluginStatus::InvalidArg;
        }
        inited_ = true;
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        ++update_called_;
        if (mode_ == Mode::UpdateThrow) {
            throw std::runtime_error("update throw");
        }
        if (!inited_) {
            if (out_error) *out_error = "not initialized";
            return PluginStatus::InternalError;
        }
        if (mode_ == Mode::UpdateFail) {
            if (out_error) *out_error = "update fail";
            return PluginStatus::InternalError;
        }
        if (mode_ == Mode::UpdateTimeout) {
            if (out_error) *out_error = "timeout";
            return PluginStatus::Timeout;
        }

        out = BehaviorOutput{};
        out.param_targets["p.test"] = 0.42f;
        out.param_weights["p.test"] = 1.0f;
        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        ++destroy_called_;
        inited_ = false;
    }

    int init_called() const { return init_called_; }
    int update_called() const { return update_called_; }
    int destroy_called() const { return destroy_called_; }

private:
    Mode mode_;
    bool inited_ = false;
    int init_called_ = 0;
    int update_called_ = 0;
    int destroy_called_ = 0;
};

class FlakyCrashPlugin final : public IBehaviorPlugin {
public:
    PluginStatus Init(const PluginRuntimeConfig &,
                      const PluginHostCallbacks &,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        if (!out_desc) {
            if (out_error) *out_error = "out_desc null";
            return PluginStatus::InvalidArg;
        }
        *out_desc = PluginDescriptor{"flaky_crash", "1.0.0", "worker_isolation"};
        inited_ = true;
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        if (!inited_) {
            if (out_error) *out_error = "not initialized";
            return PluginStatus::InternalError;
        }

        ++tick_;
        if ((tick_ % 2) == 0) {
            throw std::runtime_error("simulated plugin crash");
        }

        out = BehaviorOutput{};
        out.param_targets["p.test"] = static_cast<float>(tick_);
        out.param_weights["p.test"] = 1.0f;
        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        inited_ = false;
    }

private:
    bool inited_ = false;
    int tick_ = 0;
};

class EchoInputPlugin final : public IBehaviorPlugin {
public:
    PluginStatus Init(const PluginRuntimeConfig &,
                      const PluginHostCallbacks &,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        if (!out_desc) {
            if (out_error) *out_error = "out_desc null";
            return PluginStatus::InvalidArg;
        }
        *out_desc = PluginDescriptor{"echo_input", "1.0.0", "queue_validation"};
        inited_ = true;
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &in,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        if (!inited_) {
            if (out_error) *out_error = "not initialized";
            return PluginStatus::InternalError;
        }

        out = BehaviorOutput{};
        out.param_targets["p.test"] = static_cast<float>(in.time_sec);
        out.param_weights["p.test"] = 1.0f;
        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        inited_ = false;
    }

private:
    bool inited_ = false;
};

class SlowEchoInputPlugin final : public IBehaviorPlugin {
public:
    PluginStatus Init(const PluginRuntimeConfig &,
                      const PluginHostCallbacks &,
                      PluginDescriptor *out_desc,
                      std::string *out_error) override {
        if (!out_desc) {
            if (out_error) *out_error = "out_desc null";
            return PluginStatus::InvalidArg;
        }
        *out_desc = PluginDescriptor{"slow_echo_input", "1.0.0", "queue_bounding"};
        inited_ = true;
        return PluginStatus::Ok;
    }

    PluginStatus Update(const PerceptionInput &in,
                        BehaviorOutput &out,
                        std::string *out_error) override {
        if (!inited_) {
            if (out_error) *out_error = "not initialized";
            return PluginStatus::InternalError;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        out = BehaviorOutput{};
        out.param_targets["p.test"] = static_cast<float>(in.time_sec);
        out.param_weights["p.test"] = 1.0f;
        return PluginStatus::Ok;
    }

    void Destroy() noexcept override {
        inited_ = false;
    }

private:
    bool inited_ = false;
};

bool TestInitUpdateDestroySuccess() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::Success));

    std::string err;
    const PluginStatus st_init = mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err);
    if (!Assert::Eq(st_init, PluginStatus::Ok, "success: init should be ok")) return false;
    if (!Assert::True(mgr.IsReady(), "success: manager should be ready")) return false;

    BehaviorOutput out{};
    const PluginStatus st_upd = mgr.Update(PerceptionInput{}, out, &err);
    if (!Assert::Eq(st_upd, PluginStatus::Ok, "success: update should be ok")) return false;
    if (!Assert::True(out.param_targets.find("p.test") != out.param_targets.end(), "success: output target missing")) return false;

    mgr.Destroy();
    if (!Assert::True(!mgr.IsReady(), "success: manager should not be ready after destroy")) return false;
    return true;
}

bool TestInitFail() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::InitFail));

    std::string err;
    const PluginStatus st_init = mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err);
    if (!Assert::Eq(st_init, PluginStatus::InvalidArg, "init fail: status")) return false;
    if (!Assert::Contains(err, "init fail", "init fail: error text")) return false;
    if (!Assert::True(!mgr.IsReady(), "init fail: manager should not be ready")) return false;
    return true;
}

bool TestUpdateFail() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::UpdateFail));

    std::string err;
    if (!Assert::Eq(mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err), PluginStatus::Ok, "update fail: init")) return false;

    BehaviorOutput out{};
    const PluginStatus st_upd = mgr.Update(PerceptionInput{}, out, &err);
    if (!Assert::Eq(st_upd, PluginStatus::InternalError, "update fail: status")) return false;
    if (!Assert::Contains(err, "update fail", "update fail: error text")) return false;
    return true;
}

bool TestUpdateTimeout() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::UpdateTimeout));

    std::string err;
    if (!Assert::Eq(mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err), PluginStatus::Ok, "timeout: init")) return false;

    BehaviorOutput out{};
    const PluginStatus st_upd = mgr.Update(PerceptionInput{}, out, &err);
    if (!Assert::Eq(st_upd, PluginStatus::Timeout, "timeout: status")) return false;
    if (!Assert::Contains(err, "timeout", "timeout: error text")) return false;
    return true;
}

bool TestInitException() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::InitThrow));

    std::string err;
    const PluginStatus st_init = mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err);
    if (!Assert::Eq(st_init, PluginStatus::InternalError, "init exception: status")) return false;
    if (!Assert::Contains(err, "Init exception", "init exception: manager wrapped error")) return false;
    return true;
}

bool TestUpdateException() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::UpdateThrow));

    std::string err;
    if (!Assert::Eq(mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err), PluginStatus::Ok, "update exception: init")) return false;

    BehaviorOutput out{};
    const PluginStatus st_upd = mgr.Update(PerceptionInput{}, out, &err);
    if (!Assert::Eq(st_upd, PluginStatus::InternalError, "update exception: status")) return false;
    if (!Assert::Contains(err, "Update exception", "update exception: manager wrapped error")) return false;
    return true;
}

bool TestMainLoopIsolationOnPluginCrash() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<FlakyCrashPlugin>());

    std::string err;
    if (!Assert::Eq(mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err), PluginStatus::Ok,
                    "isolation: init")) {
        return false;
    }

    desktoper2D::PluginWorker worker;
    desktoper2D::PluginWorkerConfig cfg{};
    cfg.update_hz = 120;
    cfg.frame_budget_ms = 1;
    cfg.timeout_degrade_threshold = 8;
    if (!Assert::True(worker.Start(&mgr, cfg, &err), "isolation: worker start")) {
        return false;
    }

    std::uint64_t last_seq = 0;
    int consumed_ok = 0;
    for (int i = 0; i < 80; ++i) {
        PerceptionInput in{};
        in.time_sec = i * 0.01;
        in.user_presence = 1.0f;
        worker.SubmitInput(in);

        BehaviorOutput out{};
        std::uint64_t seq = 0;
        if (worker.TryConsumeLatestOutput(out, &seq)) {
            if (!Assert::True(seq > last_seq, "isolation: seq should be monotonic")) {
                worker.Stop();
                return false;
            }
            last_seq = seq;
            ++consumed_ok;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    worker.Stop();

    if (!Assert::True(consumed_ok >= 5, "isolation: should still consume successful outputs after crashes")) {
        return false;
    }

    const desktoper2D::PluginWorkerStats stats = worker.GetStats();
    if (!Assert::True(stats.exception_count > 0, "isolation: exception count should be recorded")) {
        return false;
    }

    return true;
}

bool TestWorkerWaitsForInputBeforeFirstUpdate() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<StubPlugin>(StubPlugin::Mode::Success));

    std::string err;
    if (!Assert::Eq(mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err), PluginStatus::Ok,
                    "wait-for-input: init")) {
        return false;
    }

    desktoper2D::PluginWorker worker;
    desktoper2D::PluginWorkerConfig cfg{};
    cfg.update_hz = 120;
    cfg.frame_budget_ms = 1;
    if (!Assert::True(worker.Start(&mgr, cfg, &err), "wait-for-input: worker start")) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const desktoper2D::PluginWorkerStats idle_stats = worker.GetStats();
    if (!Assert::True(idle_stats.total_update_count == 0, "wait-for-input: worker should stay idle before first input")) {
        worker.Stop();
        return false;
    }

    PerceptionInput in{};
    in.time_sec = 0.25;
    in.user_presence = 1.0f;
    worker.SubmitInput(in);

    bool saw_update = false;
    for (int i = 0; i < 50; ++i) {
        const desktoper2D::PluginWorkerStats stats = worker.GetStats();
        if (stats.total_update_count > 0) {
            saw_update = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    worker.Stop();
    return Assert::True(saw_update, "wait-for-input: worker should wake after input submit");
}

bool TestWorkerDropsOldInputsWhenQueueIsBounded() {
    PluginManager mgr;
    mgr.SetPlugin(std::make_unique<SlowEchoInputPlugin>());

    std::string err;
    if (!Assert::Eq(mgr.Init(PluginRuntimeConfig{}, PluginHostCallbacks{}, &err), PluginStatus::Ok,
                    "queue: init")) {
        return false;
    }

    desktoper2D::PluginWorker worker;
    desktoper2D::PluginWorkerConfig cfg{};
    cfg.update_hz = 120;
    cfg.frame_budget_ms = 1;
    cfg.max_input_queue_size = 1;
    if (!Assert::True(worker.Start(&mgr, cfg, &err), "queue: worker start")) {
        return false;
    }

    constexpr int kInputCount = 6;
    for (int i = 1; i <= kInputCount; ++i) {
        PerceptionInput in{};
        in.time_sec = static_cast<double>(i);
        in.user_presence = 1.0f;
        worker.SubmitInput(in);
    }

    bool saw_drops = false;
    bool saw_latest_output = false;
    for (int i = 0; i < 300; ++i) {
        const desktoper2D::PluginWorkerStats stats = worker.GetStats();
        saw_drops = saw_drops || stats.dropped_input_count > 0;

        BehaviorOutput out{};
        if (worker.TryConsumeLatestOutput(out, nullptr)) {
            const auto it = out.param_targets.find("p.test");
            if (it != out.param_targets.end() &&
                std::fabs(it->second - static_cast<float>(kInputCount)) < 0.001f) {
                saw_latest_output = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!Assert::True(saw_drops, "queue: bounded queue should drop stale inputs under backlog")) {
        worker.Stop();
        return false;
    }
    if (!Assert::True(saw_latest_output, "queue: worker should eventually surface the latest input")) {
        worker.Stop();
        return false;
    }

    const desktoper2D::PluginWorkerStats bounded_stats = worker.GetStats();
    if (!Assert::True(bounded_stats.pending_input_count <= cfg.max_input_queue_size,
                      "queue: pending inputs should stay within queue bound")) {
        worker.Stop();
        return false;
    }
    if (!Assert::True(bounded_stats.total_update_count < kInputCount,
                      "queue: worker should avoid processing every stale input when bounded")) {
        worker.Stop();
        return false;
    }

    worker.Stop();
    return true;
}

}  // namespace

int main() {
    int failed = 0;

    if (!TestInitUpdateDestroySuccess()) ++failed;
    if (!TestInitFail()) ++failed;
    if (!TestUpdateFail()) ++failed;
    if (!TestUpdateTimeout()) ++failed;
    if (!TestInitException()) ++failed;
    if (!TestUpdateException()) ++failed;
    if (!TestMainLoopIsolationOnPluginCrash()) ++failed;
    if (!TestWorkerWaitsForInputBeforeFirstUpdate()) ++failed;
    if (!TestWorkerDropsOldInputsWhenQueueIsBounded()) ++failed;

    if (failed == 0) {
        std::cout << "[PASS] plugin lifecycle tests all passed\n";
        return 0;
    }

    std::cerr << "[FAIL] plugin lifecycle tests failed: " << failed << " case(s)\n";
    return 1;
}
