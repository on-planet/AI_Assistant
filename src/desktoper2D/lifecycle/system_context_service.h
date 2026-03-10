#pragma once

#include <string>

namespace desktoper2D {

struct SystemContextSnapshot {
    std::string process_name;
    std::string window_title;
    std::string url_hint;
};

class SystemContextService {
public:
    bool Capture(SystemContextSnapshot &out, std::string *out_error);
};

}  // namespace desktoper2D
