#include "k2d/lifecycle/plugin_lifecycle.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

using k2d::BehaviorOutput;
using k2d::IBehaviorPlugin;
using k2d::PerceptionInput;
using k2d::PluginDescriptor;
using k2d::PluginHostCallbacks;
using k2d::PluginManager;
using k2d::PluginRuntimeConfig;
using k2d::PluginStatus;

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

    k2d::PluginWorker worker;
    k2d::PluginWorkerConfig cfg{};
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

    const k2d::PluginWorkerStats stats = worker.GetStats();
    if (!Assert::True(stats.exception_count > 0, "isolation: exception count should be recorded")) {
        return false;
    }

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

    if (failed == 0) {
        std::cout << "[PASS] plugin lifecycle tests all passed\n";
        return 0;
    }

    std::cerr << "[FAIL] plugin lifecycle tests failed: " << failed << " case(s)\n";
    return 1;
}
