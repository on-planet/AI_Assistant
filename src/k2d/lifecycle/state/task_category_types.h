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
        "unity",
        "ue4",
        "unreal",
        "lobby",
        "match",
        "battle",
        "menu",
        "settlement",
    };

    std::vector<TaskCategoryKeywordRule> secondary_rules = {
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameSettlement, {"result", "settlement"}},
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameLobby, {"lobby"}},
        {TaskPrimaryCategory::Game, TaskSecondaryCategory::GameMenu, {"menu"}},
        {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkDebugging, {"debug", "gdb"}},
        {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkReadingDocs, {"readme", "docs", "wiki"}},
        {TaskPrimaryCategory::Work, TaskSecondaryCategory::WorkMeetingNotes, {"meeting", "minutes"}},
    };

    TaskSecondaryCategory default_work_secondary = TaskSecondaryCategory::WorkCoding;
    TaskSecondaryCategory default_game_secondary = TaskSecondaryCategory::GameMatch;
};

}  // namespace k2d
