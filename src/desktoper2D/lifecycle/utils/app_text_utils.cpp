#include "desktoper2D/lifecycle/utils/app_text_utils.h"

#include <cstdint>

namespace desktoper2D {

std::string MakeUtf8SafeLabel(const std::string &s) {
    if (s.empty()) return s;

    std::string out;
    out.reserve(s.size());

    bool has_invalid = false;
    const auto *p = reinterpret_cast<const unsigned char *>(s.data());
    const std::size_t n = s.size();

    for (std::size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        if (c <= 0x7F) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        }

        int need = 0;
        std::uint32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            cp = c & 0x07u;
        } else {
            has_invalid = true;
            ++i;
            continue;
        }

        if (i + static_cast<std::size_t>(need) >= n) {
            has_invalid = true;
            break;
        }

        bool ok = true;
        for (int k = 1; k <= need; ++k) {
            const unsigned char cc = p[i + static_cast<std::size_t>(k)];
            if ((cc & 0xC0u) != 0x80u) {
                ok = false;
                break;
            }
            cp = (cp << 6u) | (cc & 0x3Fu);
        }

        if (!ok) {
            has_invalid = true;
            ++i;
            continue;
        }

        if ((need == 1 && cp < 0x80u) ||
            (need == 2 && cp < 0x800u) ||
            (need == 3 && cp < 0x10000u) ||
            (cp >= 0xD800u && cp <= 0xDFFFu) ||
            (cp > 0x10FFFFu)) {
            has_invalid = true;
            ++i;
            continue;
        }

        out.append(s, i, static_cast<std::size_t>(need + 1));
        i += static_cast<std::size_t>(need + 1);
    }

    if (has_invalid) {
        return "[invalid-utf8]";
    }
    return out;
}

std::string MakeImguiAsciiSafe(const std::string &s) {
    if (s.empty()) return s;
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (ch >= 32 && ch <= 126) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('_');
        }
    }
    if (out.find_first_not_of('_') == std::string::npos) {
        return "[non-ascii-label]";
    }
    return out;
}

}  // namespace desktoper2D
