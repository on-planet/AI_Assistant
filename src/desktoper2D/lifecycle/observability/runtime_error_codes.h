#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "desktoper2D/core/async_logger.h"

namespace desktoper2D {

enum class RuntimeErrorDomain {
    None = 0,
    PerceptionCapture,
    PerceptionScene,
    PerceptionOcr,
    PerceptionFacemesh,
    PerceptionSystemContext,
    PluginWorker,
    Asr,
    DecisionHub,
    Chat,
    Reminder,
};

enum class RuntimeErrorCode {
    Ok = 0,
    ResourceNotFound,
    InvalidConfig,
    PermissionDenied,
    DependencyUnavailable,
    DeviceUnavailable,
    NetworkUnavailable,
    AuthFailed,
    RateLimited,
    InitFailed,
    CaptureFailed,
    InferenceFailed,
    DataQualityDegraded,
    TimeoutDegraded,
    AutoDisabled,
    MemoryLimitExceeded,
    UnsupportedOperation,
    ModelMismatch,
    InternalError,
};

struct RuntimeErrorInfo {
    RuntimeErrorDomain domain = RuntimeErrorDomain::None;
    RuntimeErrorCode code = RuntimeErrorCode::Ok;
    std::string detail;
    std::int64_t count = 0;          // hard error 次数
    std::int64_t degraded_count = 0; // 降级但可运行次数
    std::int64_t first_seen_ts_ms = 0;
    std::int64_t last_seen_ts_ms = 0;
    std::int64_t last_recovered_ts_ms = 0;
};

inline const char *RuntimeErrorDomainName(RuntimeErrorDomain domain) {
    switch (domain) {
        case RuntimeErrorDomain::PerceptionCapture: return "perception.capture";
        case RuntimeErrorDomain::PerceptionScene: return "perception.scene";
        case RuntimeErrorDomain::PerceptionOcr: return "perception.ocr";
        case RuntimeErrorDomain::PerceptionFacemesh: return "perception.facemesh";
        case RuntimeErrorDomain::PerceptionSystemContext: return "perception.system_context";
        case RuntimeErrorDomain::PluginWorker: return "plugin.worker";
        case RuntimeErrorDomain::Asr: return "asr";
        case RuntimeErrorDomain::DecisionHub: return "decision.hub";
        case RuntimeErrorDomain::Chat: return "chat";
        case RuntimeErrorDomain::Reminder: return "reminder";
        default: return "none";
    }
}

inline const char *RuntimeErrorCodeName(RuntimeErrorCode code) {
    switch (code) {
        case RuntimeErrorCode::ResourceNotFound: return "E_RESOURCE_NOT_FOUND";
        case RuntimeErrorCode::InvalidConfig: return "E_INVALID_CONFIG";
        case RuntimeErrorCode::PermissionDenied: return "E_PERMISSION_DENIED";
        case RuntimeErrorCode::DependencyUnavailable: return "E_DEPENDENCY_UNAVAILABLE";
        case RuntimeErrorCode::DeviceUnavailable: return "E_DEVICE_UNAVAILABLE";
        case RuntimeErrorCode::NetworkUnavailable: return "E_NETWORK_UNAVAILABLE";
        case RuntimeErrorCode::AuthFailed: return "E_AUTH_FAILED";
        case RuntimeErrorCode::RateLimited: return "E_RATE_LIMITED";
        case RuntimeErrorCode::InitFailed: return "E_INIT_FAILED";
        case RuntimeErrorCode::CaptureFailed: return "E_CAPTURE_FAILED";
        case RuntimeErrorCode::InferenceFailed: return "E_INFERENCE_FAILED";
        case RuntimeErrorCode::DataQualityDegraded: return "E_DATA_QUALITY_DEGRADED";
        case RuntimeErrorCode::TimeoutDegraded: return "E_TIMEOUT_DEGRADED";
        case RuntimeErrorCode::AutoDisabled: return "E_AUTO_DISABLED";
        case RuntimeErrorCode::MemoryLimitExceeded: return "E_MEMORY_LIMIT_EXCEEDED";
        case RuntimeErrorCode::UnsupportedOperation: return "E_UNSUPPORTED_OPERATION";
        case RuntimeErrorCode::ModelMismatch: return "E_MODEL_MISMATCH";
        case RuntimeErrorCode::InternalError: return "E_INTERNAL";
        default: return "OK";
    }
}

inline RuntimeErrorCode ClassifyRuntimeErrorCodeFromDetail(const std::string &detail,
                                                           RuntimeErrorCode fallback = RuntimeErrorCode::InternalError) {
    const auto has = [&](const char *needle) {
        return detail.find(needle) != std::string::npos;
    };

    if (has("not found") || has("not exist") || has("No such file") || has("cannot open") || has("missing")) {
        return RuntimeErrorCode::ResourceNotFound;
    }
    if (has("permission") || has("access denied") || has("denied")) {
        return RuntimeErrorCode::PermissionDenied;
    }
    if (has("config") || has("invalid arg") || has("invalid parameter") || has("parse")) {
        return RuntimeErrorCode::InvalidConfig;
    }
    if (has("out of memory") || has("bad alloc") || has("bad_alloc") || has("memory") || has("alloc")) {
        return RuntimeErrorCode::MemoryLimitExceeded;
    }
    if (has("unsupported") || has("not implemented") || has("unsupported op") || has("unsupported operator")) {
        return RuntimeErrorCode::UnsupportedOperation;
    }
    if (has("shape mismatch") || has("dimension mismatch") || has("invalid shape") || has("tensor shape") || has("model mismatch")) {
        return RuntimeErrorCode::ModelMismatch;
    }
    if (has("camera") || has("microphone") || has("device") || has("dxgi") || has("d3d")) {
        return RuntimeErrorCode::DeviceUnavailable;
    }
    if (has("network") || has("dns") || has("connection") || has("socket") || has("http")) {
        return RuntimeErrorCode::NetworkUnavailable;
    }
    if (has("auth") || has("token") || has("unauthorized") || has("forbidden") || has("api key")) {
        return RuntimeErrorCode::AuthFailed;
    }
    if (has("429") || has("rate limit") || has("too many requests")) {
        return RuntimeErrorCode::RateLimited;
    }
    if (has("timeout")) {
        return RuntimeErrorCode::TimeoutDegraded;
    }
    if (has("onnx") || has("opencv") || has("cuda") || has("provider not found")) {
        return RuntimeErrorCode::DependencyUnavailable;
    }

    return fallback;
}

enum class ObsLogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

inline const char *ObsLogLevelName(ObsLogLevel level) {
    switch (level) {
        case ObsLogLevel::Debug: return "DEBUG";
        case ObsLogLevel::Info: return "INFO";
        case ObsLogLevel::Warn: return "WARN";
        case ObsLogLevel::Error: return "ERROR";
        case ObsLogLevel::Fatal: return "FATAL";
        default: return "INFO";
    }
}

inline std::int64_t ObsNowTsMs() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<std::int64_t>(ms.count());
}

inline std::uint64_t NextObsEventId() {
    static std::atomic<std::uint64_t> g_event_id{1};
    return g_event_id.fetch_add(1, std::memory_order_relaxed);
}

inline std::string ObsEscapeJson(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "?";
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

inline std::string BuildObsEventJson(std::int64_t ts,
                                     ObsLogLevel level,
                                     const char *domain,
                                     const char *code,
                                     const char *module,
                                     const std::string &detail,
                                     std::uint64_t event_id,
                                     const std::string &trace_id) {
    std::string line;
    line.reserve(256 + detail.size() + trace_id.size());
    line += "{\"ts\":";
    line += std::to_string(ts);
    line += ",\"level\":\"";
    line += ObsLogLevelName(level);
    line += "\",\"domain\":\"";
    line += ObsEscapeJson(domain ? domain : "none");
    line += "\",\"code\":\"";
    line += ObsEscapeJson(code ? code : "OK");
    line += "\",\"module\":\"";
    line += ObsEscapeJson(module ? module : "unknown");
    line += "\",\"detail\":\"";
    line += ObsEscapeJson(detail);
    line += "\",\"event_id\":";
    line += std::to_string(event_id);
    line += ",\"trace_id\":\"";
    line += ObsEscapeJson(trace_id);
    line += "\"}";
    return line;
}

inline void LogObsEvent(ObsLogLevel level,
                        const char *domain,
                        const char *code,
                        const char *module,
                        const std::string &detail,
                        const std::string &trace_id = "runtime-main") {
    const std::string line = BuildObsEventJson(ObsNowTsMs(),
                                               level,
                                               domain,
                                               code,
                                               module,
                                               detail,
                                               NextObsEventId(),
                                               trace_id);
    AsyncLogger::Instance().LogRaw(line);
}

inline void LogObsInfo(const char *domain,
                       const char *code,
                       const char *module,
                       const std::string &detail,
                       const std::string &trace_id = "runtime-main") {
    LogObsEvent(ObsLogLevel::Info, domain, code, module, detail, trace_id);
}

inline void LogObsError(const char *domain,
                        const char *code,
                        const char *module,
                        const std::string &detail,
                        const std::string &trace_id = "runtime-main") {
    LogObsEvent(ObsLogLevel::Error, domain, code, module, detail, trace_id);
}

inline void RecordRuntimeError(RuntimeErrorInfo &dst,
                               RuntimeErrorDomain domain,
                               RuntimeErrorCode code,
                               std::string detail) {
    const std::int64_t now_ts = ObsNowTsMs();
    if (dst.count == 0 && dst.degraded_count == 0) {
        dst.first_seen_ts_ms = now_ts;
    }
    dst.domain = domain;
    dst.code = code;
    dst.detail = std::move(detail);
    dst.count += 1;
    dst.last_seen_ts_ms = now_ts;
}

inline void RecordRuntimeDegrade(RuntimeErrorInfo &dst,
                                 RuntimeErrorDomain domain,
                                 RuntimeErrorCode code,
                                 std::string detail) {
    const std::int64_t now_ts = ObsNowTsMs();
    if (dst.count == 0 && dst.degraded_count == 0) {
        dst.first_seen_ts_ms = now_ts;
    }
    dst.domain = domain;
    dst.code = code;
    dst.detail = std::move(detail);
    dst.degraded_count += 1;
    dst.last_seen_ts_ms = now_ts;
}

inline void UpdateRuntimeError(RuntimeErrorInfo &dst,
                               RuntimeErrorDomain domain,
                               RuntimeErrorCode code,
                               const std::string &detail) {
    if (dst.domain == domain && dst.code == code && dst.detail == detail) {
        dst.count += 1;
        dst.last_seen_ts_ms = ObsNowTsMs();
        return;
    }
    RecordRuntimeError(dst, domain, code, detail);
}

inline void UpdateRuntimeDegrade(RuntimeErrorInfo &dst,
                                 RuntimeErrorDomain domain,
                                 RuntimeErrorCode code,
                                 const std::string &detail) {
    if (dst.domain == domain && dst.code == code && dst.detail == detail) {
        dst.degraded_count += 1;
        dst.last_seen_ts_ms = ObsNowTsMs();
        return;
    }
    RecordRuntimeDegrade(dst, domain, code, detail);
}

inline RuntimeErrorCode ResolvePluginErrorCode(RuntimeErrorCode code, const std::string &detail) {
    if (code == RuntimeErrorCode::InternalError || code == RuntimeErrorCode::InitFailed || code == RuntimeErrorCode::InferenceFailed) {
        return ClassifyRuntimeErrorCodeFromDetail(detail, code);
    }
    return code;
}

inline void SetPluginError(RuntimeErrorInfo &dst,
                           RuntimeErrorCode code,
                           const std::string &detail) {
    UpdateRuntimeError(dst,
                       RuntimeErrorDomain::PluginWorker,
                       ResolvePluginErrorCode(code, detail),
                       detail);
}

inline void SetPluginDegrade(RuntimeErrorInfo &dst,
                             RuntimeErrorCode code,
                             const std::string &detail) {
    UpdateRuntimeDegrade(dst,
                         RuntimeErrorDomain::PluginWorker,
                         ResolvePluginErrorCode(code, detail),
                         detail);
}

inline void ClearRuntimeError(RuntimeErrorInfo &dst,
                              const char *module = nullptr,
                              const std::string &recovery_reason = "",
                              const std::string &trace_id = "runtime-main") {
    const bool had_error = (dst.code != RuntimeErrorCode::Ok || dst.count > 0 || dst.degraded_count > 0);
    const RuntimeErrorDomain prev_domain = dst.domain;
    const RuntimeErrorCode prev_code = dst.code;
    const std::string prev_detail = dst.detail;
    const std::int64_t prev_first_seen = dst.first_seen_ts_ms;
    const std::int64_t prev_last_seen = dst.last_seen_ts_ms;
    const std::int64_t prev_count = dst.count;
    const std::int64_t prev_degraded_count = dst.degraded_count;

    if (had_error) {
        dst.last_recovered_ts_ms = ObsNowTsMs();
    }

    if (had_error && module != nullptr && module[0] != '\0') {
        const std::int64_t baseline_ts = prev_last_seen > 0 ? prev_last_seen : prev_first_seen;
        const std::int64_t elapsed_ms = baseline_ts > 0 ? std::max<std::int64_t>(0, dst.last_recovered_ts_ms - baseline_ts) : 0;

        std::string detail = "recovery_reason=";
        detail += recovery_reason.empty() ? "unknown" : recovery_reason;
        detail += " recovered_from=";
        detail += RuntimeErrorDomainName(prev_domain);
        detail += ".";
        detail += RuntimeErrorCodeName(prev_code);
        detail += " prev_detail=";
        detail += prev_detail;
        detail += " elapsed_ms=";
        detail += std::to_string(static_cast<long long>(elapsed_ms));
        detail += " prev_count=";
        detail += std::to_string(static_cast<long long>(prev_count));
        detail += " prev_degraded=";
        detail += std::to_string(static_cast<long long>(prev_degraded_count));

        LogObsInfo(RuntimeErrorDomainName(prev_domain),
                   "RECOVERED",
                   module,
                   detail,
                   trace_id);
    }

    dst.domain = RuntimeErrorDomain::None;
    dst.code = RuntimeErrorCode::Ok;
    dst.detail.clear();
}

}  // namespace desktoper2D
