#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

#include "desktoper2D/lifecycle/state/app_runtime_state.h"

namespace desktoper2D {

using ParamGroup = std::pair<std::string, std::vector<int>>;

std::string DetectParamPrefix(const std::string &param_id);
std::string DetectParamSemanticGroup(const std::string &param_id);
bool ParamMatchesSearch(const std::string &param_id, const char *search_text);
std::vector<ParamGroup> BuildParamGroups(const AppRuntime &runtime, int group_mode, const char *search_text);

}  // namespace desktoper2D
