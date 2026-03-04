#pragma once

#include <string>

namespace k2d {

std::string MakeUtf8SafeLabel(const std::string &s);
std::string MakeImguiAsciiSafe(const std::string &s);

}  // namespace k2d
