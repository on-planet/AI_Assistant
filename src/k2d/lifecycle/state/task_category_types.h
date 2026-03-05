#pragma once

#include <string>
#include <vector>

namespace k2d {

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

struct TaskCategoryConfig {
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

}  // namespace k2d
