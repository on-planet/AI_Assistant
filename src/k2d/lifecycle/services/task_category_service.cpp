#include "k2d/lifecycle/services/task_category_service.h"

#include <algorithm>
#include <cctype>
#include <string>

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

TaskSecondaryCategory InferSecondaryCategory(const std::string &text,
                                             TaskPrimaryCategory primary,
                                             const TaskCategoryConfig &config) {
    for (const auto &rule : config.secondary_rules) {
        if (rule.primary != primary || rule.secondary == TaskSecondaryCategory::Unknown) {
            continue;
        }
        if (ContainsAny(text, rule.keywords)) {
            return rule.secondary;
        }
    }

    if (primary == TaskPrimaryCategory::Game) {
        return config.default_game_secondary != TaskSecondaryCategory::Unknown
                   ? config.default_game_secondary
                   : TaskSecondaryCategory::GameMatch;
    }

    return config.default_work_secondary != TaskSecondaryCategory::Unknown
               ? config.default_work_secondary
               : TaskSecondaryCategory::WorkCoding;
}

}  // namespace

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       const TaskCategoryConfig &config,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary) {
    std::string text = ctx.process_name + "\n" + ctx.window_title + "\n" + ctx.url_hint + "\n" + ocr.summary + "\n" + scene.label;
    text = ToLower(text);

    out_primary = ContainsAny(text, config.game_primary_keywords)
                      ? TaskPrimaryCategory::Game
                      : TaskPrimaryCategory::Work;

    out_secondary = InferSecondaryCategory(text, out_primary, config);
}

}  // namespace k2d
