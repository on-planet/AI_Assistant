#pragma once

#include <string>

namespace k2d {

struct SystemContextSnapshot {
    std::string process_name;
    std::string window_title;
    std::string url_hint;
};

class SystemContextService {
public:
    bool Capture(SystemContextSnapshot &out, std::string *out_error);
};

}  // namespace k2d
