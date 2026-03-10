#pragma once

#include <string>
#include <vector>

#include "desktoper2D/lifecycle/plugin_lifecycle.h"

namespace desktoper2D {

PluginRouteConfig NormalizeRouteConfig(PluginRouteConfig cfg);

struct RouteCandidateScore {
    std::string name;
    float opacity_bias = 0.0f;
    float scene_score = 0.0f;
    float task_score = 0.0f;
    float structured_score = 0.0f;
    float presence_score = 0.0f;
    float total_score = 0.0f;
};

struct RouteMatch {
    std::string name = "unknown";
    float opacity_bias = 0.0f;
    float scene_score = 0.0f;
    float task_score = 0.0f;
    float structured_score = 0.0f;
    float presence_score = 0.0f;
    float total_score = 0.0f;
    std::vector<RouteCandidateScore> rejected;
};

class PluginRouteDecision {
public:
    explicit PluginRouteDecision(PluginRouteConfig config);

    RouteMatch Resolve(const PerceptionInput &in);

private:
    static std::string ToLowerCopy(const std::string &s);
    static int CountKeywordHits(const std::string &text_lower, const std::vector<std::string> &keywords);

    PluginRouteConfig config_{};
    std::string last_selected_route_;
    int route_hold_frames_left_ = 0;
    int route_cooldown_frames_left_ = 0;
};

}  // namespace desktoper2D
