#include "desktoper2D/core/async_logger.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace desktoper2D {
namespace {

std::string BuildDefaultLogFilePath() {
    std::error_code ec;
    std::filesystem::create_directories("logs", ec);

    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif

    std::ostringstream oss;
    oss << "logs/app_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".log";
    return oss.str();
}

void SDLCALL LoggerOutputFunction(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    (void)userdata;
    (void)category;

    const char *level = "INFO";
    if (priority >= SDL_LOG_PRIORITY_ERROR) {
        level = "ERROR";
    } else if (priority == SDL_LOG_PRIORITY_WARN) {
        level = "WARN";
    } else if (priority == SDL_LOG_PRIORITY_DEBUG || priority == SDL_LOG_PRIORITY_VERBOSE) {
        level = "DEBUG";
    }

    AsyncLogger::Instance().Log(level, "%s", message ? message : "");
}

void CrashTerminateHandler() {
    AsyncLogger::Instance().Log("FATAL", "std::terminate captured (possible crash)");
    AsyncLogger::Instance().Flush();
    std::_Exit(3);
}

}  // namespace

AsyncLogger::~AsyncLogger() {
    Stop();
}

AsyncLogger &AsyncLogger::Instance() {
    static AsyncLogger inst;
    return inst;
}

bool AsyncLogger::Start(const std::string &log_file_path) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (running_.load()) {
        return true;
    }

    out_.open(log_file_path, std::ios::out | std::ios::app);
    if (!out_.is_open()) {
        return false;
    }

    log_file_path_ = log_file_path;
    stop_requested_ = false;
    running_.store(true);
    worker_ = std::thread([this]() { WorkerMain(); });
    return true;
}

void AsyncLogger::Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!running_.load()) return;
        // Disable accepting new logs first to avoid shutdown deadlock.
        running_.store(false);
        stop_requested_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
    while (!queue_.empty()) queue_.pop();
    stop_requested_ = false;
}

void AsyncLogger::Flush() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (out_.is_open()) {
        out_.flush();
    }
}

void AsyncLogger::Log(const char *level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    VLog(level, fmt, args);
    va_end(args);
}

void AsyncLogger::VLog(const char *level, const char *fmt, va_list args) {
    char stack_buf[2048];
    va_list args_copy;
    va_copy(args_copy, args);
    const int written = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, args_copy);
    va_end(args_copy);

    std::string msg;
    if (written < 0) {
        msg = "[format_error]";
    } else if (static_cast<size_t>(written) < sizeof(stack_buf)) {
        msg.assign(stack_buf, static_cast<size_t>(written));
    } else {
        std::vector<char> dyn(static_cast<size_t>(written) + 1);
        std::vsnprintf(dyn.data(), dyn.size(), fmt, args);
        msg.assign(dyn.data(), static_cast<size_t>(written));
    }

    LogRaw(BuildTimestampPrefix() + " [" + (level ? std::string(level) : std::string("INFO")) + "] " + msg);
}

void AsyncLogger::LogRaw(const std::string &line) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!running_.load()) return;
    queue_.push(line);
    cv_.notify_one();
}

std::string AsyncLogger::LogFilePath() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return log_file_path_;
}

void AsyncLogger::WorkerMain() {
    for (;;) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]() { return stop_requested_ || !queue_.empty(); });

        while (!queue_.empty()) {
            const std::string line = std::move(queue_.front());
            queue_.pop();
            if (out_.is_open()) {
                out_ << line << '\n';
            }
        }
        if (out_.is_open()) out_.flush();

        if (stop_requested_ && queue_.empty()) {
            break;
        }
    }
}

std::string AsyncLogger::BuildTimestampPrefix() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

bool InitProcessLogger() {
    const std::string path = BuildDefaultLogFilePath();
    if (!AsyncLogger::Instance().Start(path)) {
        return false;
    }

    std::set_terminate(CrashTerminateHandler);
    SDL_SetLogOutputFunction(LoggerOutputFunction, nullptr);
    AsyncLogger::Instance().Log("INFO", "logger started: %s", path.c_str());
    return true;
}

void ShutdownProcessLogger() noexcept {
    AsyncLogger::Instance().Log("INFO", "logger shutdown");
    // Detach SDL log callback first to avoid re-entrancy during shutdown.
    SDL_SetLogOutputFunction(nullptr, nullptr);
    AsyncLogger::Instance().Stop();
}

void LogInfo(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AsyncLogger::Instance().VLog("INFO", fmt, args);
    va_end(args);
}

void LogError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    AsyncLogger::Instance().VLog("ERROR", fmt, args);
    va_end(args);
}

}  // namespace desktoper2D
