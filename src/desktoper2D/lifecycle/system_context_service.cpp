#include "desktoper2D/lifecycle/system_context_service.h"

#ifdef _WIN32
#include <Windows.h>
#include <psapi.h>
#endif

#include <algorithm>

namespace desktoper2D {

bool SystemContextService::Capture(SystemContextSnapshot &out, std::string *out_error) {
    out = SystemContextSnapshot{};

#ifdef _WIN32
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        if (out_error) *out_error = "GetForegroundWindow returned null";
        return false;
    }

    wchar_t title_buf[512]{};
    const int title_len = GetWindowTextW(hwnd, title_buf, static_cast<int>(std::size(title_buf)));
    if (title_len > 0) {
        std::wstring ws(title_buf, title_buf + title_len);
        out.window_title.assign(ws.begin(), ws.end());
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        HANDLE hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hproc) {
            wchar_t proc_buf[MAX_PATH]{};
            if (GetModuleBaseNameW(hproc, nullptr, proc_buf, MAX_PATH) > 0) {
                std::wstring ws(proc_buf);
                out.process_name.assign(ws.begin(), ws.end());
            }
            CloseHandle(hproc);
        }
    }

    // URL hint：简化版，从窗口标题中猜测（浏览器常见形态）
    std::string lower = out.window_title;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("http") != std::string::npos || lower.find("www.") != std::string::npos) {
        out.url_hint = out.window_title;
    }

    if (out.process_name.empty() && out.window_title.empty()) {
        if (out_error) *out_error = "failed to query process/title";
        return false;
    }
    return true;
#else
    (void)out_error;
    return false;
#endif
}

}  // namespace desktoper2D
