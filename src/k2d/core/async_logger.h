#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace k2d {

class AsyncLogger {
public:
    static AsyncLogger &Instance();

    bool Start(const std::string &log_file_path);
    void Stop() noexcept;
    void Flush();

    void Log(const char *level, const char *fmt, ...);
    void VLog(const char *level, const char *fmt, va_list args);

    void LogRaw(const std::string &line);

    std::string LogFilePath() const;
    bool IsRunning() const noexcept { return running_.load(); }

private:
    AsyncLogger() = default;
    ~AsyncLogger();

    AsyncLogger(const AsyncLogger &) = delete;
    AsyncLogger &operator=(const AsyncLogger &) = delete;

    void WorkerMain();
    static std::string BuildTimestampPrefix();

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::string> queue_;
    std::thread worker_;
    std::ofstream out_;
    std::string log_file_path_;
    std::atomic<bool> running_{false};
    bool stop_requested_ = false;
};

bool InitProcessLogger();
void ShutdownProcessLogger() noexcept;
void LogInfo(const char *fmt, ...);
void LogError(const char *fmt, ...);

}  // namespace k2d
