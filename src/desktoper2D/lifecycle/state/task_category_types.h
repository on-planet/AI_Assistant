#pragma once

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

struct TaskCategoryCalibrationConfig {
    float scene_temperature = 1.35f;
    float ocr_platt_a = 1.10f;
    float ocr_platt_b = -0.08f;
    float context_temperature = 1.15f;
};

struct TaskCategoryConfig {
    TaskCategoryCalibrationConfig calibration{};

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
