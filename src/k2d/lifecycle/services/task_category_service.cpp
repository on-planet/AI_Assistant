#include "k2d/lifecycle/services/task_category_service.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>

namespace k2d {

const char *TaskPrimaryCategoryName(TaskPrimaryCategory c) {
    switch (c) {
        case TaskPrimaryCategory::Work: return "work";
        case TaskPrimaryCategory::Game: return "game";
        default: return "unknown";
    }
}

const char *TaskSecondaryCategoryName(TaskSecondaryCategory c) {
    switch (c) {
        case TaskSecondaryCategory::WorkCoding: return "coding";
        case TaskSecondaryCategory::WorkDebugging: return "debugging";
        case TaskSecondaryCategory::WorkReadingDocs: return "reading_docs";
        case TaskSecondaryCategory::WorkMeetingNotes: return "meeting_notes";
        case TaskSecondaryCategory::GameLobby: return "lobby";
        case TaskSecondaryCategory::GameMatch: return "match";
        case TaskSecondaryCategory::GameSettlement: return "settlement";
        case TaskSecondaryCategory::GameMenu: return "menu";
        default: return "unknown";
    }
}

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

bool ContainsAny(const std::string &text, const std::vector<std::string> &keywords) {
    for (const std::string &kw : keywords) {
        if (kw.empty()) {
            continue;
        }
        if (text.find(ToLower(kw)) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TaskSecondaryCategory CanonicalizeSecondaryCategory(TaskSecondaryCategory c) {
    switch (c) {
        // 收敛工作相关近义类，减少 coding/debugging/reading_docs 的多标签歧义。
        case TaskSecondaryCategory::WorkDebugging:
        case TaskSecondaryCategory::WorkReadingDocs: return TaskSecondaryCategory::WorkCoding;

        // 收敛游戏阶段类，减少 lobby/match/settlement/menu 边界重叠。
        case TaskSecondaryCategory::GameLobby:
        case TaskSecondaryCategory::GameSettlement:
        case TaskSecondaryCategory::GameMenu: return TaskSecondaryCategory::GameMatch;

        default: return c;
    }
}

int CountKeywordHits(const std::string &text, const std::vector<std::string> &keywords) {
    int hits = 0;
    for (const std::string &kw : keywords) {
        if (kw.empty()) {
            continue;
        }
        if (text.find(ToLower(kw)) != std::string::npos) {
            ++hits;
        }
    }
    return hits;
}

float LongTailClassBoost(TaskPrimaryCategory primary, TaskSecondaryCategory secondary) {
    if (primary == TaskPrimaryCategory::Work) {
        switch (secondary) {
            case TaskSecondaryCategory::WorkMeetingNotes: return 1.20f;
            case TaskSecondaryCategory::WorkReadingDocs: return 1.08f;
            case TaskSecondaryCategory::WorkDebugging: return 1.05f;
            default: return 1.00f;
        }
    }

    if (primary == TaskPrimaryCategory::Game) {
        switch (secondary) {
            case TaskSecondaryCategory::GameSettlement:
            case TaskSecondaryCategory::GameLobby:
            case TaskSecondaryCategory::GameMenu: return 1.15f;
            case TaskSecondaryCategory::GameMatch: return 0.92f;
            default: return 1.00f;
        }
    }

    return 1.00f;
}

struct DynamicSourceWeights {
    float scene = 0.33f;
    float ocr = 0.33f;
    float context = 0.34f;
};

float Sigmoid(float x) {
    x = std::clamp(x, -20.0f, 20.0f);
    return 1.0f / (1.0f + std::exp(-x));
}

float CalibrateByTemperature(float p, float temperature) {
    const float eps = 1e-5f;
    const float t = std::max(0.05f, temperature);
    const float pc = std::clamp(p, eps, 1.0f - eps);
    const float logit = std::log(pc / (1.0f - pc));
    return Sigmoid(logit / t);
}

float CalibrateByPlatt(float p, float a, float b) {
    const float eps = 1e-5f;
    const float pc = std::clamp(p, eps, 1.0f - eps);
    const float logit = std::log(pc / (1.0f - pc));
    return Sigmoid(a * logit + b);
}

DynamicSourceWeights BuildDynamicSourceWeights(const SystemContextSnapshot &ctx,
                                               const OcrResult &ocr,
                                               const SceneClassificationResult &scene) {
    DynamicSourceWeights w{};

    // Step-1: 先得到各路原始置信度（不可直接比大小）。
    const float scene_raw = (scene.label.empty() ? 0.0f : std::clamp(scene.score, 0.0f, 1.0f));

    float ocr_avg_conf = 0.0f;
    int ocr_count = 0;
    for (const auto &line : ocr.lines) {
        if (line.text.empty()) {
            continue;
        }
        ocr_avg_conf += std::clamp(line.score, 0.0f, 1.0f);
        ++ocr_count;
    }
    if (ocr_count > 0) {
        ocr_avg_conf /= static_cast<float>(ocr_count);
    }
    const float ocr_coverage = std::min(1.0f, static_cast<float>(ocr_count) / 6.0f);
    const float ocr_raw = 0.65f * ocr_avg_conf + 0.35f * ocr_coverage;

    const bool has_process = !ctx.process_name.empty();
    const bool has_title = !ctx.window_title.empty();
    const bool has_url = !ctx.url_hint.empty();
    const float context_raw = (has_process ? 0.45f : 0.0f) + (has_title ? 0.30f : 0.0f) + (has_url ? 0.25f : 0.0f);

    // Step-2: 分数校准，让三路置信度处于可比空间。
    // scene: temperature scaling（软化偏激高分）
    const float scene_cal = CalibrateByTemperature(scene_raw, 1.35f);
    // ocr: platt scaling（拟合为更稳定后验）
    const float ocr_cal = CalibrateByPlatt(ocr_raw, 1.10f, -0.08f);
    // context: temperature scaling（离散模板分布拉平）
    const float context_cal = CalibrateByTemperature(context_raw, 1.15f);

    // Step-3: 在校准后置信度上做动态加权。
    w.scene = 0.10f + 0.90f * scene_cal;
    w.ocr = 0.10f + 0.90f * ocr_cal;
    w.context = 0.10f + 0.90f * context_cal;

    const float sum = w.scene + w.ocr + w.context;
    if (sum > 1e-6f) {
        w.scene /= sum;
        w.ocr /= sum;
        w.context /= sum;
    }
    return w;
}

struct StructuredFeatureLexicon {
    std::vector<std::string> action_terms;
    std::vector<std::string> time_terms;
    std::vector<std::string> object_terms;
};

struct ExplicitSentenceSignals {
    float negation = 0.0f;
    float conditional = 0.0f;
    float multi_intent = 0.0f;
};

StructuredFeatureLexicon BuildPrimaryStructuredLexicon(TaskPrimaryCategory primary) {
    if (primary == TaskPrimaryCategory::Game) {
        return {
            {"queue", "matchmaking", "battle", "fight", "rank", "play", "开黑", "排位", "对局", "战斗"},
            {"daily", "today", "tonight", "season", "countdown", "限时", "今日", "本周", "赛季"},
            {"lobby", "match", "round", "minimap", "boss", "mission", "party", "地图", "副本", "任务"},
        };
    }

    return {
        {"code", "debug", "read", "review", "write", "compile", "fix"},
        {"today", "tomorrow", "deadline", "schedule", "meeting"},
        {"document", "api", "issue", "stacktrace", "ticket", "spec"},
    };
}

StructuredFeatureLexicon BuildSecondaryStructuredLexicon(TaskPrimaryCategory primary,
                                                         TaskSecondaryCategory secondary) {
    if (primary == TaskPrimaryCategory::Game) {
        switch (secondary) {
            case TaskSecondaryCategory::GameLobby:
                return {{"queue", "ready", "invite", "matchmaking", "组队", "匹配"},
                        {"daily", "today", "countdown", "活动", "今日", "限时"},
                        {"lobby", "party", "room", "character", "英雄", "大厅", "房间"}};
            case TaskSecondaryCategory::GameSettlement:
                return {{"finish", "settle", "result", "win", "lose", "结算", "复盘"},
                        {"end", "post", "after", "回合结束", "赛后"},
                        {"scoreboard", "mvp", "summary", "战绩", "评分", "数据"}};
            case TaskSecondaryCategory::GameMenu:
                return {{"configure", "adjust", "select", "equip", "设置", "切换", "选择"},
                        {"before", "pre", "start", "开局前", "准备阶段"},
                        {"menu", "settings", "inventory", "store", "loadout", "菜单", "背包", "商店"}};
            case TaskSecondaryCategory::GameMatch:
            default:
                return {{"attack", "aim", "fight", "push", "defend", "战斗", "对枪", "推进"},
                        {"round", "wave", "phase", "本回合", "当前局"},
                        {"crosshair", "objective", "ammo", "minimap", "据点", "地图", "弹药"}};
        }
    }

    switch (secondary) {
        case TaskSecondaryCategory::WorkDebugging:
            return {{"debug", "trace", "inspect", "fix", "排查", "修复", "定位"},
                    {"now", "urgent", "today", "当前", "今天", "紧急"},
                    {"exception", "stacktrace", "breakpoint", "assert", "崩溃", "日志"}};
        case TaskSecondaryCategory::WorkReadingDocs:
            return {{"read", "learn", "search", "阅读", "学习", "查阅"},
                    {"plan", "week", "schedule", "本周", "计划", "日程"},
                    {"docs", "manual", "wiki", "api", "spec", "文档", "手册", "规范"}};
        case TaskSecondaryCategory::WorkMeetingNotes:
            return {{"discuss", "review", "sync", "record", "讨论", "同步", "记录"},
                    {"tomorrow", "next", "agenda", "明天", "下周", "议程"},
                    {"meeting", "minutes", "action items", "todo", "会议", "纪要", "待办"}};
        case TaskSecondaryCategory::WorkCoding:
        default:
            return {{"implement", "refactor", "build", "develop", "编码", "实现", "重构", "开发"},
                    {"deadline", "iteration", "milestone", "截止", "迭代", "里程碑"},
                    {"module", "feature", "cmake", "repo", "代码", "模块", "功能"}};
    }
}

float ComputeStructuredScore(const std::array<std::pair<const std::string *, float>, 3> &source_texts,
                             const StructuredFeatureLexicon &lexicon) {
    constexpr float kActionWeight = 1.25f;
    constexpr float kTimeWeight = 0.85f;
    constexpr float kObjectWeight = 1.10f;

    const float lexicon_scale = std::max(
        1.0f,
        static_cast<float>(lexicon.action_terms.size() + lexicon.time_terms.size() + lexicon.object_terms.size()));

    float total = 0.0f;
    for (const auto &source : source_texts) {
        if (!source.first || source.second <= 0.0f) {
            continue;
        }
        const float action_hits = static_cast<float>(CountKeywordHits(*source.first, lexicon.action_terms));
        const float time_hits = static_cast<float>(CountKeywordHits(*source.first, lexicon.time_terms));
        const float object_hits = static_cast<float>(CountKeywordHits(*source.first, lexicon.object_terms));
        const float source_structured =
            (kActionWeight * action_hits + kTimeWeight * time_hits + kObjectWeight * object_hits) / lexicon_scale;
        total += source_structured * source.second;
    }

    return total;
}

ExplicitSentenceSignals ExtractExplicitSentenceSignals(const std::array<std::pair<const std::string *, float>, 3> &source_texts) {
    const std::vector<std::string> negation_terms = {
        "不是", "不要", "不需要", "无需", "并非", "not", "no need", "don't", "do not"
    };
    const std::vector<std::string> conditional_terms = {
        "如果", "若", "假如", "when", "if", "unless", "只要", "一旦"
    };
    const std::vector<std::string> intent_connectors = {
        "并且", "同时", "然后", "以及", "且", "and", "then", "plus", "+", ";", "；"
    };

    ExplicitSentenceSignals signals{};
    for (const auto &source : source_texts) {
        if (!source.first || source.second <= 0.0f) {
            continue;
        }
        const float w = source.second;
        const int neg_hits = CountKeywordHits(*source.first, negation_terms);
        const int cond_hits = CountKeywordHits(*source.first, conditional_terms);
        const int conn_hits = CountKeywordHits(*source.first, intent_connectors);

        if (neg_hits > 0) {
            signals.negation += std::min(1.0f, 0.55f + 0.20f * static_cast<float>(neg_hits - 1)) * w;
        }
        if (cond_hits > 0) {
            signals.conditional += std::min(1.0f, 0.45f + 0.20f * static_cast<float>(cond_hits - 1)) * w;
        }
        if (conn_hits > 0) {
            signals.multi_intent += std::min(1.0f, 0.40f + 0.22f * static_cast<float>(conn_hits - 1)) * w;
        }
    }

    signals.negation = std::clamp(signals.negation, 0.0f, 1.0f);
    signals.conditional = std::clamp(signals.conditional, 0.0f, 1.0f);
    signals.multi_intent = std::clamp(signals.multi_intent, 0.0f, 1.0f);
    return signals;
}

float ComputeExplicitSignalAdjustment(TaskPrimaryCategory primary,
                                      TaskSecondaryCategory secondary,
                                      const ExplicitSentenceSignals &signals) {
    // negation: 抑制带有“提醒/会议计划”语义的类别，避免“不是提醒”被误判。
    float neg_adjust = 0.0f;
    if (signals.negation > 0.0f) {
        if (secondary == TaskSecondaryCategory::WorkMeetingNotes ||
            secondary == TaskSecondaryCategory::GameLobby) {
            neg_adjust -= 0.32f * signals.negation;
        } else {
            neg_adjust -= 0.10f * signals.negation;
        }
    }

    // conditional: 条件句更偏执行/动作导向，适度提升 coding/match。
    float cond_adjust = 0.0f;
    if (signals.conditional > 0.0f) {
        if (secondary == TaskSecondaryCategory::WorkCoding ||
            secondary == TaskSecondaryCategory::GameMatch) {
            cond_adjust += 0.20f * signals.conditional;
        } else {
            cond_adjust += 0.06f * signals.conditional;
        }
    }

    // multi-intent: 多意图句通常存在并列任务，降低“强行单类高置信”风险。
    float multi_adjust = 0.0f;
    if (signals.multi_intent > 0.0f) {
        if (primary == TaskPrimaryCategory::Work) {
            multi_adjust += 0.08f * signals.multi_intent;
        } else {
            multi_adjust += 0.05f * signals.multi_intent;
        }
    }

    return neg_adjust + cond_adjust + multi_adjust;
}

TaskSecondaryCategory InferSecondaryCategory(const std::string &scene_text,
                                             const std::string &ocr_text,
                                             const std::string &ctx_text,
                                             const DynamicSourceWeights &w,
                                             TaskPrimaryCategory primary,
                                             const TaskCategoryConfig &config) {
    const std::array<std::pair<const std::string *, float>, 3> source_texts = {
        std::pair<const std::string *, float>{&scene_text, w.scene},
        std::pair<const std::string *, float>{&ocr_text, w.ocr},
        std::pair<const std::string *, float>{&ctx_text, w.context},
    };
    const ExplicitSentenceSignals signals = ExtractExplicitSentenceSignals(source_texts);

    // Stage-1: 粗分类，先做候选集收缩（Top-K），减少相近类别互相干扰。
    std::unordered_map<TaskSecondaryCategory, float> coarse_score_by_class;
    for (const auto &rule : config.secondary_rules) {
        if (rule.primary != primary || rule.secondary == TaskSecondaryCategory::Unknown) {
            continue;
        }

        float weighted_hits = 0.0f;
        for (const auto &source : source_texts) {
            if (!source.first || source.second <= 0.0f) {
                continue;
            }
            const int hits = CountKeywordHits(*source.first, rule.keywords);
            if (hits > 0) {
                weighted_hits += static_cast<float>(hits) * source.second;
            }
        }
        if (weighted_hits <= 0.0f) {
            continue;
        }

        const float rarity_weight = 1.0f / std::sqrt(static_cast<float>(std::max<std::size_t>(1, rule.keywords.size())));
        // 粗排阶段用更稳健的 sqrt 压缩，避免极端高命中直接“拍死”其他近邻类。
        const float coarse_piece = std::sqrt(weighted_hits) * rarity_weight;
        coarse_score_by_class[rule.secondary] += coarse_piece;
    }

    std::vector<std::pair<TaskSecondaryCategory, float>> coarse_ranked;
    coarse_ranked.reserve(coarse_score_by_class.size());
    for (const auto &kv : coarse_score_by_class) {
        coarse_ranked.push_back(kv);
    }
    std::sort(coarse_ranked.begin(), coarse_ranked.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    std::vector<TaskSecondaryCategory> stage2_candidates;
    stage2_candidates.reserve(2);
    for (std::size_t i = 0; i < coarse_ranked.size() && i < 2; ++i) {
        stage2_candidates.push_back(coarse_ranked[i].first);
    }

    // Stage-2: 候选集内精排（细粒度长尾/困难样本项只在候选类内竞争）。
    std::unordered_map<TaskSecondaryCategory, float> refined_score_by_class;
    for (const auto &rule : config.secondary_rules) {
        if (rule.primary != primary || rule.secondary == TaskSecondaryCategory::Unknown) {
            continue;
        }

        if (!stage2_candidates.empty() &&
            std::find(stage2_candidates.begin(), stage2_candidates.end(), rule.secondary) == stage2_candidates.end()) {
            continue;
        }

        float weighted_hits = 0.0f;
        for (const auto &source : source_texts) {
            if (!source.first || source.second <= 0.0f) {
                continue;
            }
            const int hits = CountKeywordHits(*source.first, rule.keywords);
            if (hits > 0) {
                weighted_hits += static_cast<float>(hits) * source.second;
            }
        }
        if (weighted_hits <= 0.0f) {
            continue;
        }

        const TaskSecondaryCategory cls = CanonicalizeSecondaryCategory(rule.secondary);
        const float rarity_weight = 1.0f / std::sqrt(static_cast<float>(std::max<std::size_t>(1, rule.keywords.size())));
        const float hard_example_bonus = 1.0f + std::min(0.36f, 0.12f * std::max(0.0f, weighted_hits - 1.0f));
        const float class_boost = LongTailClassBoost(primary, cls);

        float coarse_prior = 1.0f;
        const auto coarse_it = coarse_score_by_class.find(rule.secondary);
        if (coarse_it != coarse_score_by_class.end() && !coarse_ranked.empty()) {
            const float top = std::max(1e-6f, coarse_ranked.front().second);
            coarse_prior += 0.15f * std::clamp(coarse_it->second / top, 0.0f, 1.0f);
        }

        const float semantic_score = weighted_hits * rarity_weight * hard_example_bonus * class_boost * coarse_prior;
        const StructuredFeatureLexicon structured_lexicon = BuildSecondaryStructuredLexicon(primary, cls);
        const float structured_score = ComputeStructuredScore(source_texts, structured_lexicon);

        constexpr float kSemanticWeight = 0.72f;
        constexpr float kStructuredWeight = 0.28f;
        const float explicit_adjust = ComputeExplicitSignalAdjustment(primary, cls, signals);
        const float fused_score = semantic_score * kSemanticWeight + structured_score * kStructuredWeight + explicit_adjust;

        refined_score_by_class[cls] += std::max(0.0f, fused_score);
    }

    TaskSecondaryCategory best = TaskSecondaryCategory::Unknown;
    float best_score = 0.0f;
    float second_best_score = 0.0f;
    for (const auto &kv : refined_score_by_class) {
        if (kv.second > best_score) {
            second_best_score = best_score;
            best_score = kv.second;
            best = kv.first;
        } else if (kv.second > second_best_score) {
            second_best_score = kv.second;
        }
    }
    if (best != TaskSecondaryCategory::Unknown) {
        // 低置信度兜底：不做错误硬判，返回 Unknown 作为“需澄清”状态。
        const float abs_conf_threshold = 0.20f;
        const float margin_ratio = (second_best_score > 1e-6f) ? (best_score / second_best_score) : 10.0f;
        const float rel_margin_threshold = 1.12f;
        if (best_score < abs_conf_threshold || margin_ratio < rel_margin_threshold) {
            return TaskSecondaryCategory::Unknown;
        }
        return best;
    }

    if (primary == TaskPrimaryCategory::Game) {
        const TaskSecondaryCategory fallback = config.default_game_secondary != TaskSecondaryCategory::Unknown
                                                  ? config.default_game_secondary
                                                  : TaskSecondaryCategory::GameMatch;
        return CanonicalizeSecondaryCategory(fallback);
    }

    const TaskSecondaryCategory fallback = config.default_work_secondary != TaskSecondaryCategory::Unknown
                                               ? config.default_work_secondary
                                               : TaskSecondaryCategory::WorkCoding;
    return CanonicalizeSecondaryCategory(fallback);
}

struct PrimaryInferenceResult {
    TaskPrimaryCategory primary = TaskPrimaryCategory::Work;
    float game_score = 0.0f;
    float work_score = 0.0f;
    float confidence = 0.0f;
};

PrimaryInferenceResult InferPrimaryCategoryHierarchical(const std::string &scene_text,
                                                        const std::string &ocr_text,
                                                        const std::string &ctx_text,
                                                        const DynamicSourceWeights &w,
                                                        const TaskCategoryConfig &config) {
    const std::array<std::pair<const std::string *, float>, 3> source_texts = {
        std::pair<const std::string *, float>{&scene_text, w.scene},
        std::pair<const std::string *, float>{&ocr_text, w.ocr},
        std::pair<const std::string *, float>{&ctx_text, w.context},
    };

    const float game_semantic =
        static_cast<float>(CountKeywordHits(scene_text, config.game_primary_keywords)) * w.scene +
        static_cast<float>(CountKeywordHits(ocr_text, config.game_primary_keywords)) * w.ocr +
        static_cast<float>(CountKeywordHits(ctx_text, config.game_primary_keywords)) * w.context;

    const std::vector<std::string> work_primary_keywords = {
        "code", "debug", "docs", "api", "meeting", "spec", "cmake", "review",
        "开发", "调试", "文档", "评审", "会议", "需求", "实现", "修复"
    };
    const float work_semantic =
        static_cast<float>(CountKeywordHits(scene_text, work_primary_keywords)) * w.scene +
        static_cast<float>(CountKeywordHits(ocr_text, work_primary_keywords)) * w.ocr +
        static_cast<float>(CountKeywordHits(ctx_text, work_primary_keywords)) * w.context;

    const float game_structured = ComputeStructuredScore(source_texts, BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Game));
    const float work_structured = ComputeStructuredScore(source_texts, BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Work));

    const ExplicitSentenceSignals primary_signals = ExtractExplicitSentenceSignals(source_texts);
    const float explicit_game_bias = primary_signals.negation * (-0.15f) +
                                     primary_signals.conditional * 0.12f +
                                     primary_signals.multi_intent * 0.08f;
    const float explicit_work_bias = primary_signals.negation * 0.12f +
                                     primary_signals.conditional * 0.05f +
                                     primary_signals.multi_intent * 0.06f;

    constexpr float kPrimarySemanticWeight = 0.72f;
    constexpr float kPrimaryStructuredWeight = 0.23f;
    constexpr float kPrimaryExplicitWeight = 0.05f;

    PrimaryInferenceResult result{};
    result.game_score = game_semantic * kPrimarySemanticWeight +
                        game_structured * kPrimaryStructuredWeight +
                        explicit_game_bias * kPrimaryExplicitWeight;
    result.work_score = work_semantic * kPrimarySemanticWeight +
                        work_structured * kPrimaryStructuredWeight +
                        explicit_work_bias * kPrimaryExplicitWeight;

    result.primary = (result.game_score >= result.work_score) ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
    const float top = std::max(result.game_score, result.work_score);
    const float second = std::min(result.game_score, result.work_score);
    result.confidence = (top <= 1e-6f) ? 0.0f : std::clamp((top - second) / (std::fabs(top) + 1e-6f), 0.0f, 1.0f);
    return result;
}

}  // namespace

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       const TaskCategoryConfig &config,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary) {
    const std::string scene_text = ToLower(scene.label);
    const std::string ocr_text = ToLower(ocr.summary);
    const std::string ctx_text = ToLower(ctx.process_name + "\n" + ctx.window_title + "\n" + ctx.url_hint);

    const DynamicSourceWeights w = BuildDynamicSourceWeights(ctx, ocr, scene);
    const PrimaryInferenceResult primary_result =
        InferPrimaryCategoryHierarchical(scene_text, ocr_text, ctx_text, w, config);
    out_primary = primary_result.primary;

    constexpr float kPrimaryConsistencyThreshold = 0.08f;
    if (primary_result.confidence < kPrimaryConsistencyThreshold) {
        out_secondary = TaskSecondaryCategory::Unknown;
        return;
    }

    out_secondary = InferSecondaryCategory(scene_text, ocr_text, ctx_text, w, out_primary, config);
}

}  // namespace k2d

