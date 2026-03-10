#include "desktoper2D/lifecycle/plugin_route_decision.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace desktoper2D {

PluginRouteConfig NormalizeRouteConfig(PluginRouteConfig cfg) {
    if (cfg.default_route.empty()) {
        cfg.default_route = "unknown";
    }
    cfg.switch_hold_frames = std::max(0, cfg.switch_hold_frames);
    cfg.switch_cooldown_frames = std::max(0, cfg.switch_cooldown_frames);
    cfg.switch_score_margin = std::max(0.0f, cfg.switch_score_margin);
    cfg.activation_threshold = std::max(0.0f, cfg.activation_threshold);
    cfg.expert_gating_temperature = std::max(0.05f, cfg.expert_gating_temperature);
    return cfg;
}

PluginRouteDecision::PluginRouteDecision(PluginRouteConfig config)
    : config_(NormalizeRouteConfig(std::move(config))) {}

std::string PluginRouteDecision::ToLowerCopy(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

int PluginRouteDecision::CountKeywordHits(const std::string &text_lower, const std::vector<std::string> &keywords) {
    int hits = 0;
    for (const std::string &kw : keywords) {
        if (kw.empty()) continue;
        if (text_lower.find(ToLowerCopy(kw)) != std::string::npos) {
            hits += 1;
        }
    }
    return hits;
}

RouteMatch PluginRouteDecision::Resolve(const PerceptionInput &in) {
    const std::string scene = ToLowerCopy(in.scene_label);
    const std::string task = ToLowerCopy(in.task_label);
    const float presence = std::clamp(in.vision.user_presence, 0.0f, 1.0f);
    const bool visible_hint = in.state.window_visible;
    const bool click_through_hint = in.state.click_through;

    RouteMatch best{};
    best.name = config_.default_route.empty() ? std::string("unknown") : config_.default_route;

    std::vector<RouteCandidateScore> signal_candidates;
    std::vector<RouteCandidateScore> all_rejected;
    for (const PluginRouteRule &rule : config_.rules) {
        if (rule.name.empty()) continue;

        const int scene_hits = CountKeywordHits(scene, rule.scene_keywords);
        const int task_hits = CountKeywordHits(task, rule.task_keywords);
        const float scene_score = static_cast<float>(scene_hits) * rule.scene_weight;
        const float task_score = static_cast<float>(task_hits) * rule.task_weight;
        const float structured_score =
            std::clamp(in.routing.primary_structured_confidence, 0.0f, 1.0f) * rule.primary_weight +
            std::clamp(in.routing.secondary_structured_confidence, 0.0f, 1.0f) * rule.secondary_weight;
        const float presence_score = presence * (visible_hint ? 0.35f : 0.15f) + (click_through_hint ? -0.10f : 0.05f);
        const float total_score = scene_score + task_score + structured_score * rule.structured_weight + presence_score;

        RouteCandidateScore cand{
            .name = rule.name,
            .opacity_bias = rule.opacity_bias,
            .scene_score = scene_score,
            .task_score = task_score,
            .structured_score = structured_score,
            .presence_score = presence_score,
            .total_score = total_score,
        };

        const bool has_signal = scene_hits > 0 || task_hits > 0 || structured_score > 0.0f;
        if (has_signal) {
            signal_candidates.push_back(cand);
        } else {
            all_rejected.push_back(cand);
        }
    }

    std::sort(signal_candidates.begin(), signal_candidates.end(), [](const RouteCandidateScore &a, const RouteCandidateScore &b) {
        if (std::abs(a.total_score - b.total_score) < 1e-6f) {
            return a.scene_score > b.scene_score;
        }
        return a.total_score > b.total_score;
    });

    auto find_candidate = [](const std::vector<RouteCandidateScore> &cands, const std::string &name) -> const RouteCandidateScore * {
        for (const auto &cand : cands) {
            if (cand.name == name) return &cand;
        }
        return nullptr;
    };

    const RouteCandidateScore *top_signal = signal_candidates.empty() ? nullptr : &signal_candidates.front();

    if (route_hold_frames_left_ > 0) {
        --route_hold_frames_left_;
    }
    if (route_cooldown_frames_left_ > 0) {
        --route_cooldown_frames_left_;
    }

    if (last_selected_route_.empty()) {
        last_selected_route_ = best.name;
    }

    const RouteCandidateScore *current_cand = find_candidate(signal_candidates, last_selected_route_);
    const float current_total_score = current_cand ? current_cand->total_score : 0.0f;

    if (top_signal && top_signal->total_score >= config_.activation_threshold) {
        const bool candidate_is_new = top_signal->name != last_selected_route_;
        const bool hold_ready = route_hold_frames_left_ <= 0;
        const bool cooldown_ready = route_cooldown_frames_left_ <= 0;
        const bool margin_ready = (top_signal->total_score - current_total_score) >= config_.switch_score_margin;

        if (!candidate_is_new) {
            last_selected_route_ = top_signal->name;
        } else if (hold_ready && cooldown_ready && margin_ready) {
            last_selected_route_ = top_signal->name;
            route_hold_frames_left_ = std::max(0, config_.switch_hold_frames);
            route_cooldown_frames_left_ = std::max(0, config_.switch_cooldown_frames);
        }
    }

    const RouteCandidateScore *selected_cand = find_candidate(signal_candidates, last_selected_route_);
    if (selected_cand) {
        best.name = selected_cand->name;
        best.opacity_bias = selected_cand->opacity_bias;
        best.scene_score = selected_cand->scene_score;
        best.task_score = selected_cand->task_score;
        best.structured_score = selected_cand->structured_score;
        best.presence_score = selected_cand->presence_score;
        best.total_score = selected_cand->total_score;
    } else {
        best.name = config_.default_route.empty() ? std::string("unknown") : config_.default_route;
    }

    for (const auto &cand : signal_candidates) {
        if (cand.name != best.name) {
            all_rejected.push_back(cand);
        }
    }
    best.rejected = std::move(all_rejected);
    std::sort(best.rejected.begin(), best.rejected.end(), [](const RouteCandidateScore &a, const RouteCandidateScore &b) {
        return a.total_score > b.total_score;
    });
    return best;
}

}  // namespace desktoper2D
