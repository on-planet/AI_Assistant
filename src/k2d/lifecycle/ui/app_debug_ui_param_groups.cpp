#include "k2d/lifecycle/ui/app_debug_ui_internal.h"

namespace k2d {

std::string DetectParamPrefix(const std::string &param_id) {
    if (param_id.empty()) return "(empty)";
    const std::size_t us = param_id.find('_');
    if (us != std::string::npos && us > 0) {
        return param_id.substr(0, us);
    }
    std::size_t cut = 0;
    while (cut < param_id.size()) {
        const unsigned char ch = static_cast<unsigned char>(param_id[cut]);
        if ((ch >= 'A' && ch <= 'Z' && cut > 0) || (ch >= '0' && ch <= '9')) {
            break;
        }
        ++cut;
    }
    if (cut == 0) return "misc";
    return param_id.substr(0, cut);
}

std::string DetectParamSemanticGroup(const std::string &param_id) {
    std::string id_lower = param_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (id_lower.find("eye") != std::string::npos || id_lower.find("blink") != std::string::npos) return "眼睛";
    if (id_lower.find("head") != std::string::npos || id_lower.find("neck") != std::string::npos) return "头部";
    if (id_lower.find("brow") != std::string::npos || id_lower.find("mouth") != std::string::npos ||
        id_lower.find("lip") != std::string::npos || id_lower.find("cheek") != std::string::npos ||
        id_lower.find("nose") != std::string::npos || id_lower.find("emotion") != std::string::npos ||
        id_lower.find("smile") != std::string::npos || id_lower.find("angry") != std::string::npos) {
        return "表情";
    }
    if (id_lower.find("window") != std::string::npos || id_lower.find("opacity") != std::string::npos ||
        id_lower.find("clickthrough") != std::string::npos || id_lower.find("click_through") != std::string::npos) {
        return "窗口";
    }
    if (id_lower.find("behavior") != std::string::npos || id_lower.find("idle") != std::string::npos ||
        id_lower.find("debug") != std::string::npos || id_lower.find("manual") != std::string::npos) {
        return "行为";
    }
    if (id_lower.find("hair") != std::string::npos || id_lower.find("bang") != std::string::npos) return "头发";
    if (id_lower.find("body") != std::string::npos || id_lower.find("arm") != std::string::npos) return "身体";
    return "其他";
}

bool ParamMatchesSearch(const std::string &param_id, const char *search_text) {
    if (search_text == nullptr || search_text[0] == '\0') {
        return true;
    }
    std::string id_lower = param_id;
    std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::string needle = search_text;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return id_lower.find(needle) != std::string::npos;
}

std::vector<ParamGroup> BuildParamGroups(const AppRuntime &runtime, int group_mode, const char *search_text) {
    std::map<std::string, std::vector<int>, std::less<>> grouped;
    for (int i = 0; i < static_cast<int>(runtime.model.parameters.size()); ++i) {
        const auto &p = runtime.model.parameters[static_cast<std::size_t>(i)];
        if (!ParamMatchesSearch(p.id, search_text)) {
            continue;
        }
        const std::string key = (group_mode == 1) ? DetectParamSemanticGroup(p.id) : DetectParamPrefix(p.id);
        grouped[key].push_back(i);
    }

    std::vector<ParamGroup> out;
    out.reserve(grouped.size());
    for (auto &kv : grouped) {
        out.emplace_back(kv.first, std::move(kv.second));
    }
    return out;
}

}  // namespace k2d
