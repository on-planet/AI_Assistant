#include "desktoper2D/lifecycle/services/task_category_service.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

#include "desktoper2D/lifecycle/observability/runtime_error_codes.h"

namespace desktoper2D {

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

    // Step-1: 估计各源自身置信（quality / availability）。
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

    // Step-2: 置信校准到可比空间。
    const float scene_cal = CalibrateByTemperature(scene_raw, calibration.scene_temperature);
    const float ocr_cal = CalibrateByPlatt(ocr_raw, calibration.ocr_platt_a, calibration.ocr_platt_b);
    const float context_cal = CalibrateByTemperature(context_raw, calibration.context_temperature);

    // Step-3: 用“置信驱动 + 质量门控”的 softmax 得权重（替代固定规则权重）。
    // - conf_term: 各源校准后置信
    // - quality_term: 数据完整性（如 OCR 覆盖、context 字段齐全）
    // - gate: 低置信时快速衰减，避免弱源噪声抢权重
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

struct TaskCategoryTemporalState {
    bool initialized = false;
    TaskPrimaryCategory primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory secondary = TaskSecondaryCategory::Unknown;
    float primary_conf_ema = 0.0f;
    float secondary_conf_ema = 0.0f;
    int primary_stable_frames = 0;
    int secondary_stable_frames = 0;
};

struct TaskCategoryMemoryState {
    std::deque<TaskPrimaryCategory> primary_hist;
    std::deque<TaskSecondaryCategory> secondary_hist;
    std::size_t max_hist = 12;
};

struct DecisionLayerMemory {
    enum class StateTag {
        None,
        RejectPrimary,
        JointInfer,
        RejectSecondary,
        Accept,
    };

    StateTag last_state = StateTag::None;
    int hold_frames = 0;
    float primary_conf_ema = 0.0f;
    float secondary_conf_ema = 0.0f;
    TaskPrimaryCategory locked_primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory locked_secondary = TaskSecondaryCategory::Unknown;
};

float ComputePrimaryMemoryBoost(const TaskCategoryMemoryState &mem, TaskPrimaryCategory c) {
    if (c == TaskPrimaryCategory::Unknown || mem.primary_hist.empty()) return 0.0f;
    int same = 0;
    for (TaskPrimaryCategory h : mem.primary_hist) {
        if (h == c) ++same;
    }
    return std::clamp(static_cast<float>(same) / static_cast<float>(mem.primary_hist.size()) * 0.18f, 0.0f, 0.18f);
}

float ComputeSecondaryMemoryBoost(const TaskCategoryMemoryState &mem, TaskSecondaryCategory c) {
    if (c == TaskSecondaryCategory::Unknown || mem.secondary_hist.empty()) return 0.0f;
    int same = 0;
    for (TaskSecondaryCategory h : mem.secondary_hist) {
        if (h == c) ++same;
    }
    return std::clamp(static_cast<float>(same) / static_cast<float>(mem.secondary_hist.size()) * 0.20f, 0.0f, 0.20f);
}

void UpdateMemoryCache(TaskCategoryMemoryState &mem, const TaskCategoryInferenceResult &out_result) {
    if (mem.primary_hist.size() >= mem.max_hist) mem.primary_hist.pop_front();
    if (mem.secondary_hist.size() >= mem.max_hist) mem.secondary_hist.pop_front();
    mem.primary_hist.push_back(out_result.primary);
    mem.secondary_hist.push_back(out_result.secondary);
}

void ApplyTemporalSmoothing(TaskCategoryInferenceResult &out_result) {
    static TaskCategoryTemporalState s_state{};

    constexpr float kAlpha = 0.18f;
    constexpr float kSwitchMargin = 0.10f;
    constexpr int kMinHoldFrames = 6;
    constexpr float kUnknownPrimaryKeepThreshold = 0.18f;
    constexpr float kUnknownSecondaryKeepThreshold = 0.22f;

    if (!s_state.initialized) {
        s_state.initialized = true;
        s_state.primary = out_result.primary;
        s_state.secondary = out_result.secondary;
        s_state.primary_conf_ema = std::clamp(out_result.primary_confidence, 0.0f, 1.0f);
        s_state.secondary_conf_ema = std::clamp(out_result.secondary_confidence, 0.0f, 1.0f);
        s_state.primary_stable_frames = 1;
        s_state.secondary_stable_frames = 1;
        return;
    }

    const float raw_primary_conf = std::clamp(out_result.primary_confidence, 0.0f, 1.0f);
    const float raw_secondary_conf = std::clamp(out_result.secondary_confidence, 0.0f, 1.0f);

    s_state.primary_conf_ema = s_state.primary_conf_ema * (1.0f - kAlpha) + raw_primary_conf * kAlpha;
    s_state.secondary_conf_ema = s_state.secondary_conf_ema * (1.0f - kAlpha) + raw_secondary_conf * kAlpha;

    if (out_result.primary == TaskPrimaryCategory::Unknown && raw_primary_conf <= kUnknownPrimaryKeepThreshold) {
        s_state.primary = TaskPrimaryCategory::Unknown;
        s_state.secondary = TaskSecondaryCategory::Unknown;
        s_state.primary_stable_frames = 1;
        s_state.secondary_stable_frames = 1;
    } else if (out_result.primary != s_state.primary) {
        const bool allow_switch =
            s_state.primary_stable_frames >= kMinHoldFrames &&
            raw_primary_conf >= (s_state.primary_conf_ema + kSwitchMargin);
        if (allow_switch) {
            s_state.primary = out_result.primary;
            s_state.primary_stable_frames = 1;
            s_state.secondary = out_result.secondary;
            s_state.secondary_stable_frames = 1;
        } else {
            out_result.primary = s_state.primary;
            s_state.primary_stable_frames += 1;
        }
    } else {
        s_state.primary_stable_frames += 1;
    }

    if (out_result.secondary == TaskSecondaryCategory::Unknown && raw_secondary_conf <= kUnknownSecondaryKeepThreshold) {
        s_state.secondary = TaskSecondaryCategory::Unknown;
        s_state.secondary_stable_frames = 1;
    } else if (out_result.secondary != s_state.secondary) {
        const bool allow_switch =
            s_state.secondary_stable_frames >= kMinHoldFrames &&
            raw_secondary_conf >= (s_state.secondary_conf_ema + kSwitchMargin);
        if (allow_switch) {
            s_state.secondary = out_result.secondary;
            s_state.secondary_stable_frames = 1;
        } else {
            out_result.secondary = s_state.secondary;
            s_state.secondary_stable_frames += 1;
        }
    } else {
        s_state.secondary_stable_frames += 1;
    }

    out_result.primary = s_state.primary;
    out_result.secondary = s_state.secondary;
    out_result.primary_confidence = s_state.primary_conf_ema;
    out_result.secondary_confidence = s_state.secondary_conf_ema;
}

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

bool PassWakeIntentGate(const std::string &text_lower) {
    if (text_lower.empty()) {
        return false;
    }
    const std::vector<std::string> wake_terms = {
        "desktoper2D", "assistant", "助手", "小助手", "嘿", "hello", "hi"
    };
    const std::vector<std::string> intent_terms = {
        "帮我", "请", "执行", "切换", "打开", "关闭", "提醒", "记录", "创建", "总结",
        "todo", "task", "meeting", "schedule", "code", "debug", "build", "run", "search"
    };

    const int wake_hits = CountKeywordHits(text_lower, wake_terms);
    const int intent_hits = CountKeywordHits(text_lower, intent_terms);

    // 允许两种路径：
    // 1) 明确唤醒词 + 任意意图词
    // 2) 无唤醒词但意图词足够强（>=2）
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

struct OcrStructuredSignals {
    float code_pattern = 0.0f;
    float office_pattern = 0.0f;
    float game_ui_pattern = 0.0f;
    float chat_pattern = 0.0f;
};

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
        "[", "]", "@", "聊天", "message", "reply", "在线", "离线", "pm", "am", "：", ":"
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

struct SecondaryInferenceResult {
    TaskSecondaryCategory category = TaskSecondaryCategory::Unknown;
    float confidence = 0.0f;
    float structured_confidence = 0.0f;
    std::vector<TaskCategorySecondaryCandidate> top_candidates;
};

SecondaryInferenceResult InferSecondaryCategory(const std::string &scene_text,
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
    const OcrStructuredSignals ocr_struct = ExtractOcrStructuredSignals(ocr_text);
    SecondaryInferenceResult result{};

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
        result.structured_confidence = std::max(result.structured_confidence, std::clamp(structured_score, 0.0f, 1.0f));

        constexpr float kSemanticWeight = 0.72f;
        constexpr float kStructuredWeight = 0.28f;
        constexpr float kOcrStructWeight = 0.22f;
        const float explicit_adjust = ComputeExplicitSignalAdjustment(primary, cls, signals);

        float ocr_struct_bonus = 0.0f;
        switch (cls) {
            case TaskSecondaryCategory::WorkCoding:
            case TaskSecondaryCategory::WorkDebugging:
                ocr_struct_bonus = 0.65f * ocr_struct.code_pattern + 0.20f * ocr_struct.office_pattern;
                break;
            case TaskSecondaryCategory::WorkReadingDocs:
                ocr_struct_bonus = 0.35f * ocr_struct.code_pattern + 0.55f * ocr_struct.office_pattern;
                break;
            case TaskSecondaryCategory::WorkMeetingNotes:
                ocr_struct_bonus = 0.50f * ocr_struct.office_pattern + 0.40f * ocr_struct.chat_pattern;
                break;
            case TaskSecondaryCategory::GameLobby:
            case TaskSecondaryCategory::GameMatch:
            case TaskSecondaryCategory::GameSettlement:
            case TaskSecondaryCategory::GameMenu:
                ocr_struct_bonus = 0.80f * ocr_struct.game_ui_pattern;
                break;
            default:
                break;
        }

        const float fused_score = semantic_score * kSemanticWeight +
                                  structured_score * kStructuredWeight +
                                  explicit_adjust +
                                  ocr_struct_bonus * kOcrStructWeight;

        refined_score_by_class[cls] += std::max(0.0f, fused_score);
    }

    std::vector<std::pair<TaskSecondaryCategory, float>> ranked;
    ranked.reserve(refined_score_by_class.size());
    for (const auto &kv : refined_score_by_class) {
        ranked.push_back(kv);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    for (std::size_t i = 0; i < ranked.size() && i < 3; ++i) {
        result.top_candidates.push_back(TaskCategorySecondaryCandidate{
            .category = ranked[i].first,
            .score = ranked[i].second,
        });
    }

    const TaskSecondaryCategory best = ranked.empty() ? TaskSecondaryCategory::Unknown : ranked.front().first;
    const float best_score = ranked.empty() ? 0.0f : ranked.front().second;
    const float second_best_score = ranked.size() > 1 ? ranked[1].second : 0.0f;

    if (best != TaskSecondaryCategory::Unknown) {
        const float abs_conf_threshold = 0.20f;
        const float margin_ratio = (second_best_score > 1e-6f) ? (best_score / second_best_score) : 10.0f;
        const float rel_margin_threshold = 1.12f;
        const bool accepted = best_score >= abs_conf_threshold && margin_ratio >= rel_margin_threshold;
        result.category = accepted ? best : TaskSecondaryCategory::Unknown;
        const float raw_conf = std::clamp(best_score / std::max(1.0f, best_score + second_best_score), 0.0f, 1.0f);
        result.confidence = CalibrateScoreConfidence(raw_conf, 1.20f, 1.08f, -0.04f);
        if (accepted) {
            return result;
        }
    }

    if (primary == TaskPrimaryCategory::Game) {
        const TaskSecondaryCategory fallback = config.default_game_secondary != TaskSecondaryCategory::Unknown
                                                  ? config.default_game_secondary
                                                  : TaskSecondaryCategory::GameMatch;
        result.category = CanonicalizeSecondaryCategory(fallback);
    } else {
        const TaskSecondaryCategory fallback = config.default_work_secondary != TaskSecondaryCategory::Unknown
                                                   ? config.default_work_secondary
                                                   : TaskSecondaryCategory::WorkCoding;
        result.category = CanonicalizeSecondaryCategory(fallback);
    }
    if (result.confidence <= 0.0f) {
        result.confidence = 0.15f;
    }
    return result;
}


struct PrimaryInferenceResult {
    TaskPrimaryCategory primary = TaskPrimaryCategory::Work;
    float game_score = 0.0f;
    float work_score = 0.0f;
    float game_structured_score = 0.0f;
    float work_structured_score = 0.0f;
    float confidence = 0.0f;
    float structured_confidence = 0.0f;
    float source_consistency = 0.0f;
};

TaskPrimaryCategory InferPrimaryFromSingleSource(const std::string &text,
                                                 const std::vector<std::string> &game_keywords,
                                                 const std::vector<std::string> &work_keywords) {
    const float game_semantic = static_cast<float>(CountKeywordHits(text, game_keywords));
    const float work_semantic = static_cast<float>(CountKeywordHits(text, work_keywords));
    const float game_structured = ComputeStructuredScore(
        std::array<std::pair<const std::string *, float>, 3>{
            std::pair<const std::string *, float>{&text, 1.0f},
            std::pair<const std::string *, float>{nullptr, 0.0f},
            std::pair<const std::string *, float>{nullptr, 0.0f},
        },
        BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Game));
    const float work_structured = ComputeStructuredScore(
        std::array<std::pair<const std::string *, float>, 3>{
            std::pair<const std::string *, float>{&text, 1.0f},
            std::pair<const std::string *, float>{nullptr, 0.0f},
            std::pair<const std::string *, float>{nullptr, 0.0f},
        },
        BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Work));

    const float game_score = 0.72f * game_semantic + 0.28f * game_structured;
    const float work_score = 0.72f * work_semantic + 0.28f * work_structured;
    return (game_score >= work_score) ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
}

float ComputePrimarySourceConsistency(const std::string &scene_text,
                                      const std::string &ocr_text,
                                      const std::string &ctx_text,
                                      const std::vector<std::string> &game_keywords,
                                      const std::vector<std::string> &work_keywords) {
    std::vector<TaskPrimaryCategory> votes;
    if (!scene_text.empty()) {
        votes.push_back(InferPrimaryFromSingleSource(scene_text, game_keywords, work_keywords));
    }
    if (!ocr_text.empty()) {
        votes.push_back(InferPrimaryFromSingleSource(ocr_text, game_keywords, work_keywords));
    }
    if (!ctx_text.empty()) {
        votes.push_back(InferPrimaryFromSingleSource(ctx_text, game_keywords, work_keywords));
    }

    if (votes.empty()) {
        return 0.0f;
    }
    if (votes.size() == 1) {
        return 0.55f;
    }

    int game_votes = 0;
    int work_votes = 0;
    for (TaskPrimaryCategory c : votes) {
        if (c == TaskPrimaryCategory::Game) {
            ++game_votes;
        } else {
            ++work_votes;
        }
    }

    const int total = static_cast<int>(votes.size());
    const int top_votes = std::max(game_votes, work_votes);
    const float agreement_ratio = static_cast<float>(top_votes) / static_cast<float>(total);
    const bool top1_same = (top_votes == total);
    return std::clamp(0.65f * agreement_ratio + 0.35f * (top1_same ? 1.0f : 0.0f), 0.0f, 1.0f);
}

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

    PrimaryInferenceResult result{};
    const float game_structured = ComputeStructuredScore(source_texts, BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Game));
    const float work_structured = ComputeStructuredScore(source_texts, BuildPrimaryStructuredLexicon(TaskPrimaryCategory::Work));
    result.game_structured_score = game_structured;
    result.work_structured_score = work_structured;

    const ExplicitSentenceSignals primary_signals = ExtractExplicitSentenceSignals(source_texts);
    const OcrStructuredSignals ocr_struct = ExtractOcrStructuredSignals(ocr_text);
    const float explicit_game_bias = primary_signals.negation * (-0.15f) +
                                     primary_signals.conditional * 0.12f +
                                     primary_signals.multi_intent * 0.08f;
    const float explicit_work_bias = primary_signals.negation * 0.12f +
                                     primary_signals.conditional * 0.05f +
                                     primary_signals.multi_intent * 0.06f;

    constexpr float kPrimarySemanticWeight = 0.72f;
    constexpr float kPrimaryStructuredWeight = 0.23f;
    constexpr float kPrimaryExplicitWeight = 0.05f;
    constexpr float kPrimaryOcrStructWeight = 0.18f;

    const float game_ocr_struct = 0.85f * ocr_struct.game_ui_pattern;
    const float work_ocr_struct = 0.50f * ocr_struct.code_pattern +
                                  0.35f * ocr_struct.office_pattern +
                                  0.15f * ocr_struct.chat_pattern;

    result.game_score = game_semantic * kPrimarySemanticWeight +
                        game_structured * kPrimaryStructuredWeight +
                        explicit_game_bias * kPrimaryExplicitWeight +
                        game_ocr_struct * kPrimaryOcrStructWeight;
    result.work_score = work_semantic * kPrimarySemanticWeight +
                        work_structured * kPrimaryStructuredWeight +
                        explicit_work_bias * kPrimaryExplicitWeight +
                        work_ocr_struct * kPrimaryOcrStructWeight;

    result.primary = (result.game_score >= result.work_score) ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
    const float top = std::max(result.game_score, result.work_score);
    const float second = std::min(result.game_score, result.work_score);
    const float base_confidence =
        (top <= 1e-6f) ? 0.0f : std::clamp((top - second) / (std::fabs(top) + 1e-6f), 0.0f, 1.0f);
    result.source_consistency = ComputePrimarySourceConsistency(
        scene_text,
        ocr_text,
        ctx_text,
        config.game_primary_keywords,
        work_primary_keywords);
    const float raw_primary_conf = std::clamp(base_confidence * (0.75f + 0.50f * result.source_consistency), 0.0f, 1.0f);
    result.confidence = CalibrateScoreConfidence(raw_primary_conf, 1.12f, 1.06f, -0.03f);

    const float top_structured = std::max(result.game_structured_score, result.work_structured_score);
    const float second_structured = std::min(result.game_structured_score, result.work_structured_score);
    result.structured_confidence = std::clamp(top_structured - second_structured, 0.0f, 1.0f);
    return result;
}

}  // namespace

void InferTaskCategoryDetailed(const SystemContextSnapshot &ctx,
                               const OcrResult &ocr,
                               const SceneClassificationResult &scene,
                               const TaskCategoryConfig &config,
                               const std::string *asr_session_text,
                               TaskCategoryInferenceResult &out_result,
                               RuntimeErrorInfo *out_decision_error) {
    const std::string scene_text = ToLower(scene.label);
    const std::string ocr_text = ToLower(ocr.summary);
    const std::string asr_session_raw = (asr_session_text && !asr_session_text->empty()) ? ToLower(*asr_session_text) : std::string();
    const bool asr_gate_pass = PassWakeIntentGate(asr_session_raw);

    // 多源融合：ASR 仅作为辅助上下文，不直接覆盖 system context。
    // 规则：
    // 1) 必须通过唤醒/意图门控；
    // 2) 必须有 scene 或 ocr 任一非空；
    // 3) 只取短窗，避免会话长尾“淹没”其他源。
    const std::string base_ctx_text = ToLower(ctx.process_name + "\n" + ctx.window_title + "\n" + ctx.url_hint);
    const bool has_visual_text_source = !scene_text.empty() || !ocr_text.empty();
    std::string asr_assist_text;
    if (asr_gate_pass && has_visual_text_source) {
        constexpr std::size_t kAsrAssistMaxChars = 240;
        if (asr_session_raw.size() > kAsrAssistMaxChars) {
            asr_assist_text = asr_session_raw.substr(asr_session_raw.size() - kAsrAssistMaxChars);
        } else {
            asr_assist_text = asr_session_raw;
        }
    }

    const std::string ctx_text = base_ctx_text + (asr_assist_text.empty() ? std::string() : (std::string("\n") + asr_assist_text));

    const DynamicSourceWeights w = BuildDynamicSourceWeights(ctx, ocr, scene, config.calibration);
    const PrimaryInferenceResult primary_result =
        InferPrimaryCategoryHierarchical(scene_text, ocr_text, ctx_text, w, config);

    out_result = TaskCategoryInferenceResult{};
    out_result.primary = primary_result.primary;
    out_result.primary_confidence = primary_result.confidence;
    out_result.primary_structured_confidence = primary_result.structured_confidence;
    out_result.source_scene_weight = w.scene;
    out_result.source_ocr_weight = w.ocr;
    out_result.source_context_weight = w.context;
    out_result.scene_confidence = std::clamp(scene.score, 0.0f, 1.0f);

    constexpr float kPrimaryConsistencyThreshold = 0.08f;
    constexpr float kPrimaryRejectThreshold = 0.16f;
    constexpr float kSecondaryRejectThreshold = 0.24f;
    constexpr float kMinReliableSourceWeight = 0.42f;

    enum class DecisionState {
        Idle,
        RejectPrimary,
        JointInfer,
        RejectSecondary,
        Accept,
    };

    static TaskCategoryMemoryState s_memory{};
    static DecisionLayerMemory s_decision_mem{};
    DecisionState state = DecisionState::Idle;

    const float source_peak = std::max(w.scene, std::max(w.ocr, w.context));
    const bool weak_sources = source_peak < kMinReliableSourceWeight;
    out_result.primary_confidence = std::clamp(
        out_result.primary_confidence + ComputePrimaryMemoryBoost(s_memory, out_result.primary), 0.0f, 1.0f);

    // 决策记忆层：融合历史决策置信，避免“只看当前轮输入”。
    constexpr float kDecisionAlpha = 0.22f;
    constexpr int kRejectHoldFrames = 5;
    constexpr int kSecondaryRejectHoldFrames = 4;
    s_decision_mem.primary_conf_ema =
        s_decision_mem.primary_conf_ema * (1.0f - kDecisionAlpha) + out_result.primary_confidence * kDecisionAlpha;

    const bool primary_reject_now = out_result.primary_confidence < kPrimaryRejectThreshold ||
                                    (weak_sources && out_result.primary_confidence < 0.22f);
    const bool primary_reject_mem =
        s_decision_mem.last_state == DecisionLayerMemory::StateTag::RejectPrimary &&
        s_decision_mem.hold_frames < kRejectHoldFrames &&
        s_decision_mem.primary_conf_ema < (kPrimaryRejectThreshold + 0.05f);

    const bool primary_reject = primary_reject_now || primary_reject_mem;

    // 打断/抢占：当出现高置信新主类时，允许突破历史锁。
    constexpr float kPrimaryPreemptThreshold = 0.72f;
    constexpr float kPrimaryPreemptMargin = 0.14f;
    const bool primary_preempt =
        s_decision_mem.locked_primary != TaskPrimaryCategory::Unknown &&
        out_result.primary != s_decision_mem.locked_primary &&
        out_result.primary_confidence >= kPrimaryPreemptThreshold &&
        (out_result.primary_confidence - s_decision_mem.primary_conf_ema) >= kPrimaryPreemptMargin;

    state = (out_result.primary_confidence < kPrimaryConsistencyThreshold || (primary_reject && !primary_preempt))
                ? DecisionState::RejectPrimary
                : DecisionState::JointInfer;

    if (state == DecisionState::RejectPrimary) {
        out_result.primary = TaskPrimaryCategory::Unknown;
        out_result.primary_confidence = std::min(out_result.primary_confidence, kPrimaryRejectThreshold);
        out_result.secondary = TaskSecondaryCategory::Unknown;
        out_result.secondary_confidence = 0.0f;
        out_result.secondary_structured_confidence = 0.0f;
        out_result.secondary_top_candidates.clear();
        state = DecisionState::Accept;
        if (out_decision_error) {
            UpdateRuntimeDegrade(*out_decision_error,
                                 RuntimeErrorDomain::DecisionHub,
                                 RuntimeErrorCode::DataQualityDegraded,
                                 "decision.reject_primary");
        }
    }

    if (state == DecisionState::JointInfer) {
        // 层级联合推断：secondary 不再仅在 primary 之后“硬切”。
        const SecondaryInferenceResult sec_game =
            InferSecondaryCategory(scene_text, ocr_text, ctx_text, w, TaskPrimaryCategory::Game, config);
        const SecondaryInferenceResult sec_work =
            InferSecondaryCategory(scene_text, ocr_text, ctx_text, w, TaskPrimaryCategory::Work, config);

        const float z_max = std::max(primary_result.game_score, primary_result.work_score);
        const float e_game = std::exp(primary_result.game_score - z_max);
        const float e_work = std::exp(primary_result.work_score - z_max);
        const float e_sum = e_game + e_work;
        const float p_game = (e_sum > 1e-6f) ? (e_game / e_sum) : 0.5f;
        const float p_work = (e_sum > 1e-6f) ? (e_work / e_sum) : 0.5f;

        const float game_joint = p_game * sec_game.confidence;
        const float work_joint = p_work * sec_work.confidence;

        const bool pick_game = game_joint >= work_joint;
        const SecondaryInferenceResult &joint_sec = pick_game ? sec_game : sec_work;
        out_result.primary = pick_game ? TaskPrimaryCategory::Game : TaskPrimaryCategory::Work;
        out_result.primary_confidence = CalibrateScoreConfidence(std::max(p_game, p_work), 1.10f, 1.05f, -0.02f);
        out_result.secondary = joint_sec.category;
        out_result.secondary_confidence = CalibrateScoreConfidence(std::max(game_joint, work_joint), 1.16f, 1.07f, -0.03f);
        out_result.secondary_structured_confidence = joint_sec.structured_confidence;

        std::vector<TaskCategorySecondaryCandidate> merged;
        merged.reserve(sec_game.top_candidates.size() + sec_work.top_candidates.size());
        for (const auto &c : sec_game.top_candidates) {
            merged.push_back(TaskCategorySecondaryCandidate{.category = c.category, .score = c.score * p_game});
        }
        for (const auto &c : sec_work.top_candidates) {
            merged.push_back(TaskCategorySecondaryCandidate{.category = c.category, .score = c.score * p_work});
        }
        std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
        out_result.secondary_top_candidates.clear();
        for (std::size_t i = 0; i < merged.size() && i < 3; ++i) {
            out_result.secondary_top_candidates.push_back(merged[i]);
        }

        out_result.secondary_confidence = std::clamp(
            out_result.secondary_confidence + ComputeSecondaryMemoryBoost(s_memory, out_result.secondary), 0.0f, 1.0f);

        s_decision_mem.secondary_conf_ema =
            s_decision_mem.secondary_conf_ema * (1.0f - kDecisionAlpha) + out_result.secondary_confidence * kDecisionAlpha;

        const bool secondary_reject_now =
            out_result.secondary == TaskSecondaryCategory::Unknown ||
            out_result.secondary_confidence < kSecondaryRejectThreshold ||
            (weak_sources && out_result.secondary_confidence < 0.30f);
        const bool secondary_reject_mem =
            s_decision_mem.last_state == DecisionLayerMemory::StateTag::RejectSecondary &&
            s_decision_mem.hold_frames < kSecondaryRejectHoldFrames &&
            s_decision_mem.secondary_conf_ema < (kSecondaryRejectThreshold + 0.06f);

        // 打断/抢占：高置信新次类可中断当前次类锁定。
        constexpr float kSecondaryPreemptThreshold = 0.68f;
        constexpr float kSecondaryPreemptMargin = 0.12f;
        const bool secondary_preempt =
            s_decision_mem.locked_secondary != TaskSecondaryCategory::Unknown &&
            out_result.secondary != s_decision_mem.locked_secondary &&
            out_result.secondary_confidence >= kSecondaryPreemptThreshold &&
            (out_result.secondary_confidence - s_decision_mem.secondary_conf_ema) >= kSecondaryPreemptMargin;

        if ((secondary_reject_now || secondary_reject_mem) && !secondary_preempt) {
            state = DecisionState::RejectSecondary;
        } else {
            state = DecisionState::Accept;
        }
    }

    if (state == DecisionState::RejectSecondary) {
        out_result.secondary = TaskSecondaryCategory::Unknown;
        out_result.secondary_confidence = std::min(out_result.secondary_confidence, kSecondaryRejectThreshold);
        state = DecisionState::Accept;
    }

    if (state == DecisionState::Accept) {
        ApplyTemporalSmoothing(out_result);
        UpdateMemoryCache(s_memory, out_result);
    }

    auto to_tag = [](DecisionState s) {
        switch (s) {
            case DecisionState::RejectPrimary: return DecisionLayerMemory::StateTag::RejectPrimary;
            case DecisionState::JointInfer: return DecisionLayerMemory::StateTag::JointInfer;
            case DecisionState::RejectSecondary: return DecisionLayerMemory::StateTag::RejectSecondary;
            case DecisionState::Accept: return DecisionLayerMemory::StateTag::Accept;
            case DecisionState::Idle:
            default: return DecisionLayerMemory::StateTag::None;
        }
    };
    const DecisionLayerMemory::StateTag current_tag = to_tag(state);
    if (current_tag == s_decision_mem.last_state) {
        s_decision_mem.hold_frames += 1;
    } else {
        s_decision_mem.last_state = current_tag;
        s_decision_mem.hold_frames = 1;
    }

    if (state == DecisionState::Accept) {
        s_decision_mem.locked_primary = out_result.primary;
        s_decision_mem.locked_secondary = out_result.secondary;
    }
}

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       const TaskCategoryConfig &config,
                       const std::string *asr_session_text,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary) {
    TaskCategoryInferenceResult result{};
    InferTaskCategoryDetailed(ctx, ocr, scene, config, asr_session_text, result, nullptr);
    out_primary = result.primary;
    out_secondary = result.secondary;
}

}  // namespace desktoper2D

