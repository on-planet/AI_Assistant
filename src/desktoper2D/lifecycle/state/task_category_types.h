#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace desktoper2D {

enum class TaskPrimaryCategory {
    Unknown,
    Work,
    Game,
};

enum class TaskSecondaryCategory {
    Unknown,
    WorkCoding,
    WorkDebugging,
    WorkReadingDocs,
    WorkMeetingNotes,
    GameLobby,
    GameMatch,
    GameSettlement,
    GameMenu,
};

struct TaskCategoryKeywordRule {
    TaskPrimaryCategory primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory secondary = TaskSecondaryCategory::Unknown;
    std::vector<std::string> keywords;
};

struct TaskCategorySecondaryCandidate {
    TaskSecondaryCategory category = TaskSecondaryCategory::Unknown;
    float score = 0.0f;
};

struct TaskCategoryInferenceResult {
    TaskPrimaryCategory primary = TaskPrimaryCategory::Unknown;
    float primary_confidence = 0.0f;
    float primary_structured_confidence = 0.0f;
    TaskSecondaryCategory secondary = TaskSecondaryCategory::Unknown;
    float secondary_confidence = 0.0f;
    float secondary_structured_confidence = 0.0f;
    std::vector<TaskCategorySecondaryCandidate> secondary_top_candidates;
    float source_scene_weight = 0.0f;
    float source_ocr_weight = 0.0f;
    float source_context_weight = 0.0f;
    float scene_confidence = 0.0f;
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

struct TaskCategoryDecisionMemory {
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

struct TaskCategoryCompiledKeywordRule {
    TaskPrimaryCategory primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory secondary = TaskSecondaryCategory::Unknown;
    std::vector<std::string> keywords;
};

struct TaskCategoryCompiledConfig {
    bool ready = false;
    std::uint64_t fingerprint = 0;
    std::vector<std::string> game_primary_keywords;
    std::vector<std::string> work_primary_keywords;
    std::vector<TaskCategoryCompiledKeywordRule> secondary_rules;
    TaskSecondaryCategory default_work_secondary = TaskSecondaryCategory::WorkCoding;
    TaskSecondaryCategory default_game_secondary = TaskSecondaryCategory::GameMatch;
};

struct TaskCategoryInferenceState {
    TaskCategoryTemporalState temporal{};
    TaskCategoryMemoryState memory{};
    TaskCategoryDecisionMemory decision{};
    TaskCategoryCompiledConfig compiled{};
};

struct TaskCategoryScheduleState {
    float poll_accum_sec = 0.0f;
    float min_interval_sec = 0.25f;
    std::uint64_t last_input_signature = 0;
    bool has_last_input_signature = false;
    bool force_refresh = true;
    std::uint64_t compute_count = 0;
    std::uint64_t skipped_count = 0;
};

struct TaskCategoryCalibrationConfig {
    float scene_temperature = 1.35f;
    float ocr_platt_a = 1.10f;
    float ocr_platt_b = -0.08f;
    float context_temperature = 1.15f;
};

struct TaskCategoryPrimaryConfig {
    float single_source_semantic_weight = 0.72f;
    float single_source_structured_weight = 0.28f;

    float semantic_weight = 0.72f;
    float structured_weight = 0.23f;
    float explicit_weight = 0.05f;
    float ocr_struct_weight = 0.18f;

    float explicit_game_negation_bias = -0.15f;
    float explicit_game_conditional_bias = 0.12f;
    float explicit_game_multi_intent_bias = 0.08f;

    float explicit_work_negation_bias = 0.12f;
    float explicit_work_conditional_bias = 0.05f;
    float explicit_work_multi_intent_bias = 0.06f;

    float game_ocr_ui_weight = 0.85f;
    float work_ocr_code_weight = 0.50f;
    float work_ocr_office_weight = 0.35f;
    float work_ocr_chat_weight = 0.15f;

    float confidence_consistency_bias = 0.75f;
    float confidence_consistency_weight = 0.50f;
    float confidence_temperature = 1.12f;
    float confidence_platt_a = 1.06f;
    float confidence_platt_b = -0.03f;
};

struct TaskCategoryDecisionConfig {
    std::size_t asr_assist_max_chars = 240;

    float primary_consistency_threshold = 0.08f;
    float primary_reject_threshold = 0.16f;
    float secondary_reject_threshold = 0.24f;
    float min_reliable_source_weight = 0.42f;
    float weak_source_primary_reject_threshold = 0.22f;
    float weak_source_secondary_reject_threshold = 0.30f;

    float decision_alpha = 0.22f;
    int reject_hold_frames = 5;
    int secondary_reject_hold_frames = 4;
    float primary_reject_ema_margin = 0.05f;
    float secondary_reject_ema_margin = 0.06f;

    float primary_preempt_threshold = 0.72f;
    float primary_preempt_margin = 0.14f;
    float secondary_preempt_threshold = 0.68f;
    float secondary_preempt_margin = 0.12f;

    float joint_primary_confidence_temperature = 1.10f;
    float joint_primary_confidence_platt_a = 1.05f;
    float joint_primary_confidence_platt_b = -0.02f;
    float joint_secondary_confidence_temperature = 1.16f;
    float joint_secondary_confidence_platt_a = 1.07f;
    float joint_secondary_confidence_platt_b = -0.03f;
};

struct TaskCategoryTemporalConfig {
    float ema_alpha = 0.18f;
    float switch_margin = 0.10f;
    int min_hold_frames = 6;
    float unknown_primary_keep_threshold = 0.18f;
    float unknown_secondary_keep_threshold = 0.22f;
};

struct TaskCategoryMemoryConfig {
    float primary_boost_max = 0.18f;
    float secondary_boost_max = 0.20f;
};

struct TaskCategoryConfig {
    TaskCategoryCalibrationConfig calibration{};
    TaskCategoryPrimaryConfig primary{};
    TaskCategoryDecisionConfig decision{};
    TaskCategoryTemporalConfig temporal{};
    TaskCategoryMemoryConfig memory{};

    std::vector<std::string> game_primary_keywords = {
        "game",
        "steam",
        "epic",
        "riot",
        "battlenet",
        "xbox",
        "playstation",
        "unity",
        "ue4",
        "ue5",
        "unreal",
        "lobby",
        "matchmaking",
        "match",
        "battle",
        "menu",
        "scoreboard",
        "minimap",
    };

    std::vector<std::string> work_primary_keywords = {
        "code",
        "debug",
        "docs",
        "api",
        "meeting",
        "spec",
        "cmake",
        "review",
        "development",
        "testing",
        "documentation",
        "design",
        "implementation",
        "bugfix",
        "incident",
        "analysis",
    };

    std::vector<TaskCategoryKeywordRule> secondary_rules = {
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameSettlement, {"result", "settlement", "victory", "defeat", "mvp", "scoreboard", "summary"}},
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameLobby, {"lobby", "queue", "party", "ready", "matchmaking", "character select"}},
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameMenu, {"menu", "settings", "inventory", "store", "loadout", "battle pass", "mission select"}},
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameMatch, {"round", "objective", "kill", "assist", "ammo", "crosshair"}},
        {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkDebugging, {"debug", "gdb", "lldb", "breakpoint", "traceback", "exception", "stacktrace", "assert", "crash"}},
        {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkReadingDocs, {"readme", "docs", "wiki", "manual", "handbook", "api reference", "specification", "tutorial"}},
        {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkMeetingNotes, {"meeting", "minutes", "agenda", "standup", "sync", "zoom", "teams", "feishu"}},
    };

    TaskSecondaryCategory default_work_secondary = TaskSecondaryCategory::WorkCoding;
    TaskSecondaryCategory default_game_secondary = TaskSecondaryCategory::GameMatch;
};

}  // namespace desktoper2D
