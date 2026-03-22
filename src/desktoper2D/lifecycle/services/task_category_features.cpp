#include "desktoper2D/lifecycle/services/task_category_features.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
namespace desktoper2D::task_category_internal {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

std::size_t SecondaryCategoryIndex(TaskSecondaryCategory category) {
    return static_cast<std::size_t>(category);
}

TaskPrimaryCategory PrimaryFromSecondary(TaskSecondaryCategory category) {
    switch (category) {
        case TaskSecondaryCategory::WorkCoding:
        case TaskSecondaryCategory::WorkDebugging:
        case TaskSecondaryCategory::WorkReadingDocs:
        case TaskSecondaryCategory::WorkMeetingNotes: return TaskPrimaryCategory::Work;
        case TaskSecondaryCategory::GameLobby:
        case TaskSecondaryCategory::GameMatch:
        case TaskSecondaryCategory::GameSettlement:
        case TaskSecondaryCategory::GameMenu: return TaskPrimaryCategory::Game;
        case TaskSecondaryCategory::Unknown:
        default: return TaskPrimaryCategory::Unknown;
    }
}

std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

std::uint64_t HashString(std::string_view text) {
    return static_cast<std::uint64_t>(std::hash<std::string_view>{}(text));
}

std::uint64_t ComputeCompiledConfigFingerprint(const TaskCategoryConfig &config) {
    std::uint64_t fingerprint = 0;
    for (const auto &keyword : config.game_primary_keywords) {
        fingerprint = HashCombine(fingerprint, HashString(keyword));
    }
    for (const auto &keyword : config.work_primary_keywords) {
        fingerprint = HashCombine(fingerprint, HashString(keyword));
    }
    for (const auto &rule : config.secondary_rules) {
        fingerprint = HashCombine(fingerprint, static_cast<std::uint64_t>(rule.primary));
        fingerprint = HashCombine(fingerprint, static_cast<std::uint64_t>(rule.secondary));
        for (const auto &keyword : rule.keywords) {
            fingerprint = HashCombine(fingerprint, HashString(keyword));
        }
    }
    fingerprint = HashCombine(fingerprint, static_cast<std::uint64_t>(config.default_work_secondary));
    fingerprint = HashCombine(fingerprint, static_cast<std::uint64_t>(config.default_game_secondary));
    return fingerprint;
}

TaskCategoryCompiledConfig BuildCompiledConfig(const TaskCategoryConfig &config) {
    TaskCategoryCompiledConfig compiled{};
    compiled.ready = true;
    compiled.fingerprint = ComputeCompiledConfigFingerprint(config);
    compiled.default_work_secondary = config.default_work_secondary;
    compiled.default_game_secondary = config.default_game_secondary;

    compiled.game_primary_keywords.reserve(config.game_primary_keywords.size());
    for (const auto &keyword : config.game_primary_keywords) {
        compiled.game_primary_keywords.push_back(ToLower(keyword));
    }

    compiled.work_primary_keywords.reserve(config.work_primary_keywords.size());
    for (const auto &keyword : config.work_primary_keywords) {
        compiled.work_primary_keywords.push_back(ToLower(keyword));
    }

    compiled.secondary_rules.reserve(config.secondary_rules.size());
    for (const auto &rule : config.secondary_rules) {
        TaskCategoryCompiledKeywordRule compiled_rule{};
        compiled_rule.primary = rule.primary;
        compiled_rule.secondary = rule.secondary;
        compiled_rule.keywords.reserve(rule.keywords.size());
        for (const auto &keyword : rule.keywords) {
            compiled_rule.keywords.push_back(ToLower(keyword));
        }
        compiled.secondary_rules.push_back(std::move(compiled_rule));
    }

    return compiled;
}

const TaskCategoryCompiledConfig &EnsureCompiledConfig(const TaskCategoryConfig &config,
                                                      TaskCategoryInferenceState &inout_state) {
    const std::uint64_t fingerprint = ComputeCompiledConfigFingerprint(config);
    if (!inout_state.compiled.ready || inout_state.compiled.fingerprint != fingerprint) {
        inout_state.compiled = BuildCompiledConfig(config);
    }
    return inout_state.compiled;
}

TaskSecondaryCategory CanonicalizeSecondaryCategory(TaskSecondaryCategory c) {
    return c;
}

int CountKeywordHits(const std::string &text, const std::vector<std::string> &keywords) {
    int hits = 0;
    for (const std::string &kw : keywords) {
        if (kw.empty()) {
            continue;
        }
        if (text.find(kw) != std::string::npos) {
            ++hits;
            continue;
        }

        const bool has_ascii_upper = std::any_of(kw.begin(), kw.end(), [](unsigned char ch) {
            return std::isupper(ch) != 0;
        });
        if (!has_ascii_upper) {
            continue;
        }

        if (text.find(ToLower(kw)) != std::string::npos) {
            ++hits;
        }
    }
    return hits;
}

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

float CalibrateScoreConfidence(float raw_confidence,
                               float temperature,
                               float platt_a,
                               float platt_b) {
    const float c = std::clamp(raw_confidence, 0.0f, 1.0f);
    const float t_conf = CalibrateByTemperature(c, temperature);
    return CalibrateByPlatt(t_conf, platt_a, platt_b);
}

DynamicSourceWeights BuildDynamicSourceWeights(const SystemContextSnapshot &ctx,
                                               const OcrResult &ocr,
                                               const SceneClassificationResult &scene,
                                               const TaskCategoryCalibrationConfig &calibration) {
    DynamicSourceWeights w{};

    // Step-1: 估计各源自身置信（quality / availability）�?
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
    const float ocr_raw = 0.72f * ocr_avg_conf + 0.28f * ocr_coverage;

    const bool has_process = !ctx.process_name.empty();
    const bool has_title = !ctx.window_title.empty();
    const bool has_url = !ctx.url_hint.empty();
    const float context_raw = (has_process ? 0.45f : 0.0f) + (has_title ? 0.30f : 0.0f) + (has_url ? 0.25f : 0.0f);

    // Step-2: 置信校准到可比空间�?
    const float scene_cal = CalibrateByTemperature(scene_raw, calibration.scene_temperature);
    const float ocr_cal = CalibrateByPlatt(ocr_raw, calibration.ocr_platt_a, calibration.ocr_platt_b);
    const float context_cal = CalibrateByTemperature(context_raw, calibration.context_temperature);

    // Step-3: 用“置信驱�?+ 质量门控”的 softmax 得权重（替代固定规则权重）�?
    // - conf_term: 各源校准后置�?
    // - quality_term: 数据完整性（�?OCR 覆盖、context 字段齐全�?
    // - gate: 低置信时快速衰减，避免弱源噪声抢权�?
    const float scene_quality = scene.label.empty() ? 0.0f : 1.0f;
    const float ocr_quality = std::clamp(0.35f * ocr_coverage + 0.65f * ocr_avg_conf, 0.0f, 1.0f);
    const float context_quality = std::clamp(context_raw, 0.0f, 1.0f);

    auto confidence_logit = [](float conf, float quality) {
        const float c = std::clamp(conf, 0.0f, 1.0f);
        const float q = std::clamp(quality, 0.0f, 1.0f);
        const float gate = std::clamp((c - 0.18f) / 0.55f, 0.0f, 1.0f);
        return 1.35f * c + 0.75f * q + 1.10f * gate;
    };

    const float z_scene = confidence_logit(scene_cal, scene_quality);
    const float z_ocr = confidence_logit(ocr_cal, ocr_quality);
    const float z_context = confidence_logit(context_cal, context_quality);

    const float z_max = std::max(z_scene, std::max(z_ocr, z_context));
    const float e_scene = std::exp(z_scene - z_max);
    const float e_ocr = std::exp(z_ocr - z_max);
    const float e_context = std::exp(z_context - z_max);
    const float sum = e_scene + e_ocr + e_context;

    if (sum > 1e-6f) {
        w.scene = e_scene / sum;
        w.ocr = e_ocr / sum;
        w.context = e_context / sum;
    } else {
        w.scene = 0.33f;
        w.ocr = 0.33f;
        w.context = 0.34f;
    }

    return w;
}

const StructuredFeatureLexicon &BuildPrimaryStructuredLexicon(TaskPrimaryCategory primary) {
    static const StructuredFeatureLexicon kGameLexicon = {
        {"queue", "matchmaking", "battle", "fight", "rank", "play", "start", "ranked", "duel", "combat"},
        {"daily", "today", "tonight", "season", "countdown", "限时", "今日", "本周", "赛季"},
        {"lobby", "match", "round", "minimap", "boss", "mission", "party", "地图", "副本", "任务"}
    };
    static const StructuredFeatureLexicon kWorkLexicon = {
        {"code", "debug", "read", "review", "write", "compile", "fix"},
        {"today", "tomorrow", "deadline", "schedule", "meeting"},
        {"document", "api", "issue", "stacktrace", "ticket", "spec"}
    };
    return (primary == TaskPrimaryCategory::Game) ? kGameLexicon : kWorkLexicon;
}

const StructuredFeatureLexicon &BuildSecondaryStructuredLexicon(TaskPrimaryCategory primary,
                                                                TaskSecondaryCategory secondary) {
    static const StructuredFeatureLexicon kGameLobbyLexicon = {
        {"queue", "ready", "invite", "matchmaking", "组队", "匹配"},
        {"daily", "today", "countdown", "活动", "今日", "限时"},
        {"lobby", "party", "room", "character", "英雄", "大厅", "房间"}
    };
    static const StructuredFeatureLexicon kGameSettlementLexicon = {
        {"finish", "settle", "result", "win", "lose", "结算", "复盘"},
        {"end", "post", "after", "回合结束", "赛后"},
        {"scoreboard", "mvp", "summary", "战绩", "评分", "数据"}
    };
    static const StructuredFeatureLexicon kGameMenuLexicon = {
        {"configure", "adjust", "select", "equip", "设置", "切换", "选择"},
        {"before", "pre", "start", "开局", "准备阶段"},
        {"menu", "settings", "inventory", "store", "loadout", "菜单", "背包", "商店"}
    };
    static const StructuredFeatureLexicon kGameMatchLexicon = {
        {"attack", "aim", "fight", "push", "defend", "战斗", "对枪", "推进"},
        {"round", "wave", "phase", "本回合", "当前局"},
        {"crosshair", "objective", "ammo", "minimap", "据点", "地图", "弹药"}
    };

    static const StructuredFeatureLexicon kWorkDebugLexicon = {
        {"debug", "trace", "inspect", "fix", "排查", "修复", "定位"},
        {"now", "urgent", "today", "当前", "今天", "紧急"},
        {"exception", "stacktrace", "breakpoint", "assert", "崩溃", "日志"}
    };
    static const StructuredFeatureLexicon kWorkDocsLexicon = {
        {"read", "learn", "search", "阅读", "学习", "查阅"},
        {"plan", "week", "schedule", "本周", "计划", "日程"},
        {"docs", "manual", "wiki", "api", "spec", "文档", "手册", "规范"}
    };
    static const StructuredFeatureLexicon kWorkMeetingLexicon = {
        {"discuss", "review", "sync", "record", "讨论", "同步", "记录"},
        {"tomorrow", "next", "agenda", "明天", "下周", "议程"},
        {"meeting", "minutes", "action items", "todo", "会议", "纪要", "待办"}
    };
    static const StructuredFeatureLexicon kWorkCodingLexicon = {
        {"implement", "refactor", "build", "develop", "编码", "实现", "重构", "开发"},
        {"deadline", "iteration", "milestone", "截止", "迭代", "里程碑"},
        {"module", "feature", "cmake", "repo", "代码", "模块", "功能"}
    };

    if (primary == TaskPrimaryCategory::Game) {
        switch (secondary) {
            case TaskSecondaryCategory::GameLobby: return kGameLobbyLexicon;
            case TaskSecondaryCategory::GameSettlement: return kGameSettlementLexicon;
            case TaskSecondaryCategory::GameMenu: return kGameMenuLexicon;
            case TaskSecondaryCategory::GameMatch:
            default: return kGameMatchLexicon;
        }
    }

    switch (secondary) {
        case TaskSecondaryCategory::WorkDebugging: return kWorkDebugLexicon;
        case TaskSecondaryCategory::WorkReadingDocs: return kWorkDocsLexicon;
        case TaskSecondaryCategory::WorkMeetingNotes: return kWorkMeetingLexicon;
        case TaskSecondaryCategory::WorkCoding:
        default: return kWorkCodingLexicon;
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

bool PassWakeIntentGate(const std::string &text_lower) {
    if (text_lower.empty()) {
        return false;
    }
    const std::vector<std::string> wake_terms = {
        "desktoper2D", "assistant", "助手", "小助手", "你好", "hello", "hi"
    };
    const std::vector<std::string> intent_terms = {
        "帮我", "协助", "执行", "切换", "打开", "关闭", "提醒", "记录", "创建", "总结",
        "todo", "task", "meeting", "schedule", "code", "debug", "build", "run", "search"
    };

    const int wake_hits = CountKeywordHits(text_lower, wake_terms);
    const int intent_hits = CountKeywordHits(text_lower, intent_terms);

    // 允许两种路径�?
    // 1) 明确唤醒�?+ 任意意图�?
    // 2) 无唤醒词但意图词足够强（>=2�?
    if (wake_hits > 0 && intent_hits > 0) {
        return true;
    }
    if (intent_hits >= 2) {
        return true;
    }
    return false;
}

ExplicitSentenceSignals ExtractExplicitSentenceSignals(const std::array<std::pair<const std::string *, float>, 3> &source_texts) {
    const std::vector<std::string> negation_terms = {
        "不是", "不要", "不需要", "无需", "并非", "not", "no need", "don't", "do not"
    };
    const std::vector<std::string> conditional_terms = {
        "如果", "当", "假如", "when", "if", "unless", "只要", "一旦"
    };
    const std::vector<std::string> intent_connectors = {
        "并且", "同时", "然后", "以及", "而且", "and", "then", "plus", "+", ";", "、"
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

float ComputeSingleSourceStructuredScore(const std::string &text,
                                         const StructuredFeatureLexicon &lexicon) {
    const std::array<std::pair<const std::string *, float>, kTaskSourceCount> source_texts = {
        std::pair<const std::string *, float>{&text, 1.0f},
        std::pair<const std::string *, float>{nullptr, 0.0f},
        std::pair<const std::string *, float>{nullptr, 0.0f},
    };
    return ComputeStructuredScore(source_texts, lexicon);
}

OcrStructuredSignals ExtractOcrStructuredSignals(const std::string &ocr_text) {
    OcrStructuredSignals out{};
    if (ocr_text.empty()) {
        return out;
    }

    const std::vector<std::string> code_tokens = {
        "{", "}", "()", ";", "::", "import ", "class ", "def ", "#include", "namespace", "->"
    };
    const std::vector<std::string> office_tokens = {
        "ppt", "excel", "doc", "meeting", "schedule", "agenda", "minutes", "calendar", "slide", "表格", "文档", "会议"
    };
    const std::vector<std::string> game_ui_tokens = {
        "hp", "mp", "地图", "背包", "队伍", "击杀", "任务", "等级", "score", "kill", "quest", "inventory", "minimap"
    };
    const std::vector<std::string> chat_tokens = {
        "[", "]", "@", "聊天", "message", "reply", "在线", "离线", "pm", "am", ":"
    };

    const int code_hits = CountKeywordHits(ocr_text, code_tokens);
    const int office_hits = CountKeywordHits(ocr_text, office_tokens);
    const int game_hits = CountKeywordHits(ocr_text, game_ui_tokens);
    const int chat_hits = CountKeywordHits(ocr_text, chat_tokens);

    auto to_conf = [](int hits, float base, float slope) {
        if (hits <= 0) return 0.0f;
        return std::clamp(base + slope * static_cast<float>(hits - 1), 0.0f, 1.0f);
    };

    out.code_pattern = to_conf(code_hits, 0.40f, 0.12f);
    out.office_pattern = to_conf(office_hits, 0.38f, 0.10f);
    out.game_ui_pattern = to_conf(game_hits, 0.42f, 0.11f);
    out.chat_pattern = to_conf(chat_hits, 0.34f, 0.10f);
    return out;
}

TaskCategoryFeatureSnapshot BuildTaskCategoryFeatureSnapshot(
    const SystemContextSnapshot &ctx,
    const OcrResult &ocr,
    const SceneClassificationResult &scene,
    const TaskCategoryConfig &config,
    const TaskCategoryCompiledConfig &compiled,
    const std::string *asr_session_text) {
    TaskCategoryFeatureSnapshot snapshot{};
    snapshot.scene_text = ToLower(scene.label);
    snapshot.ocr_text = ToLower(ocr.summary);

    const std::string asr_session_raw =
        (asr_session_text && !asr_session_text->empty()) ? ToLower(*asr_session_text) : std::string();
    const bool asr_gate_pass = PassWakeIntentGate(asr_session_raw);
    const std::string base_ctx_text = ToLower(ctx.process_name + "\n" + ctx.window_title + "\n" + ctx.url_hint);
    const bool has_visual_text_source = !snapshot.scene_text.empty() || !snapshot.ocr_text.empty();
    std::string asr_assist_text;
    if (asr_gate_pass && has_visual_text_source) {
        const std::size_t asr_assist_max_chars = config.decision.asr_assist_max_chars;
        if (asr_assist_max_chars > 0) {
            if (asr_session_raw.size() > asr_assist_max_chars) {
                asr_assist_text = asr_session_raw.substr(asr_session_raw.size() - asr_assist_max_chars);
            } else {
                asr_assist_text = asr_session_raw;
            }
        }
    }
    snapshot.ctx_text = base_ctx_text +
                        (asr_assist_text.empty() ? std::string() : (std::string("\n") + asr_assist_text));

    snapshot.source_weights = BuildDynamicSourceWeights(ctx, ocr, scene, config.calibration);
    snapshot.source_texts = {
        std::pair<const std::string *, float>{&snapshot.scene_text, snapshot.source_weights.scene},
        std::pair<const std::string *, float>{&snapshot.ocr_text, snapshot.source_weights.ocr},
        std::pair<const std::string *, float>{&snapshot.ctx_text, snapshot.source_weights.context},
    };
    snapshot.explicit_signals = ExtractExplicitSentenceSignals(snapshot.source_texts);
    snapshot.ocr_struct = ExtractOcrStructuredSignals(snapshot.ocr_text);

    const std::array<const std::string *, kTaskSourceCount> source_strings = {
        &snapshot.scene_text,
        &snapshot.ocr_text,
        &snapshot.ctx_text,
    };

    for (std::size_t source_idx = 0; source_idx < source_strings.size(); ++source_idx) {
        const std::string &text = *source_strings[source_idx];
        snapshot.game_primary_hits[source_idx] = CountKeywordHits(text, compiled.game_primary_keywords);
        snapshot.work_primary_hits[source_idx] = CountKeywordHits(text, compiled.work_primary_keywords);
        snapshot.game_primary_structured_scores[source_idx] =
            ComputeSingleSourceStructuredScore(text, BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Game));
        snapshot.work_primary_structured_scores[source_idx] =
            ComputeSingleSourceStructuredScore(text, BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Work));
    }

    snapshot.secondary_rule_hits.resize(compiled.secondary_rules.size(), std::array<int, kTaskSourceCount>{0, 0, 0});
    for (std::size_t rule_idx = 0; rule_idx < compiled.secondary_rules.size(); ++rule_idx) {
        for (std::size_t source_idx = 0; source_idx < source_strings.size(); ++source_idx) {
            snapshot.secondary_rule_hits[rule_idx][source_idx] =
                CountKeywordHits(*source_strings[source_idx], compiled.secondary_rules[rule_idx].keywords);
        }
    }

    for (std::size_t category_idx = 1; category_idx < kTaskSecondaryCategoryCount; ++category_idx) {
        const TaskSecondaryCategory category = static_cast<TaskSecondaryCategory>(category_idx);
        const TaskPrimaryCategory primary = PrimaryFromSecondary(category);
        if (primary == TaskPrimaryCategory::Unknown) {
            continue;
        }
        snapshot.secondary_structured_scores[category_idx] =
            ComputeStructuredScore(snapshot.source_texts, BuildSecondaryStructuredLexicon(primary, category));
    }

    return snapshot;
}

}  // namespace desktoper2D::task_category_internal
