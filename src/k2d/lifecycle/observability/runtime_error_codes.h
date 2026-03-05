#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace k2d {

enum class RuntimeErrorDomain {
    None = 0,
    PerceptionCapture,
    PerceptionScene,
    PerceptionOcr,
    PerceptionFacemesh,
    PerceptionSystemContext,
    PluginWorker,
    Asr,
    Chat,
    Reminder,
};

enum class RuntimeErrorCode {
    Ok = 0,
    ResourceNotFound,
    InitFailed,
    CaptureFailed,
    InferenceFailed,
    TimeoutDegraded,
    AutoDisabled,
    InternalError,
};

struct RuntimeErrorInfo {
    RuntimeErrorDomain domain = RuntimeErrorDomain::None;
    RuntimeErrorCode code = RuntimeErrorCode::Ok;
    std::string detail;
    std::int64_t count = 0;
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
        case RuntimeErrorDomain::Chat: return "chat";
        case RuntimeErrorDomain::Reminder: return "reminder";
        default: return "none";
    }
}

inline const char *RuntimeErrorCodeName(RuntimeErrorCode code) {
    switch (code) {
        case RuntimeErrorCode::ResourceNotFound: return "E_RESOURCE_NOT_FOUND";
        case RuntimeErrorCode::InitFailed: return "E_INIT_FAILED";
        case RuntimeErrorCode::CaptureFailed: return "E_CAPTURE_FAILED";
        case RuntimeErrorCode::InferenceFailed: return "E_INFERENCE_FAILED";
        case RuntimeErrorCode::TimeoutDegraded: return "E_TIMEOUT_DEGRADED";
        case RuntimeErrorCode::AutoDisabled: return "E_AUTO_DISABLED";
        case RuntimeErrorCode::InternalError: return "E_INTERNAL";
        default: return "OK";
    }
}

inline void RecordRuntimeError(RuntimeErrorInfo &dst,
                               RuntimeErrorDomain domain,
                               RuntimeErrorCode code,
                               std::string detail) {
    dst.domain = domain;
    dst.code = code;
    dst.detail = std::move(detail);
    dst.count += 1;
}

inline void UpdateRuntimeError(RuntimeErrorInfo &dst,
                               RuntimeErrorDomain domain,
                               RuntimeErrorCode code,
                               const std::string &detail) {
    if (dst.domain == domain && dst.code == code && dst.detail == detail) {
        return;
    }
    RecordRuntimeError(dst, domain, code, detail);
}

inline void ClearRuntimeError(RuntimeErrorInfo &dst) {
    dst.domain = RuntimeErrorDomain::None;
    dst.code = RuntimeErrorCode::Ok;
    dst.detail.clear();
}

}  // namespace k2d
