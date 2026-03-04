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

void InferTaskCategory(const SystemContextSnapshot &ctx,
                       const OcrResult &ocr,
                       const SceneClassificationResult &scene,
                       TaskPrimaryCategory &out_primary,
                       TaskSecondaryCategory &out_secondary) {
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return s;
    };

    std::string text = ctx.process_name + "\n" + ctx.window_title + "\n" + ctx.url_hint + "\n" + ocr.summary + "\n" + scene.label;
    text = to_lower(text);

    out_primary = TaskPrimaryCategory::Unknown;
    out_secondary = TaskSecondaryCategory::Unknown;

    const bool has_game_hint =
        text.find("game") != std::string::npos ||
        text.find("steam") != std::string::npos ||
        text.find("unity") != std::string::npos ||
        text.find("ue4") != std::string::npos ||
        text.find("unreal") != std::string::npos ||
        text.find("lobby") != std::string::npos ||
        text.find("match") != std::string::npos ||
        text.find("battle") != std::string::npos ||
        text.find("menu") != std::string::npos ||
        text.find("settlement") != std::string::npos ||
        text.find("lobby") != std::string::npos ||
        text.find("match") != std::string::npos;

    if (has_game_hint) {
        out_primary = TaskPrimaryCategory::Game;
        if (text.find("result") != std::string::npos || text.find("settlement") != std::string::npos) {
            out_secondary = TaskSecondaryCategory::GameSettlement;
        } else if (text.find("lobby") != std::string::npos) {
            out_secondary = TaskSecondaryCategory::GameLobby;
        } else if (text.find("menu") != std::string::npos) {
            out_secondary = TaskSecondaryCategory::GameMenu;
        } else {
            out_secondary = TaskSecondaryCategory::GameMatch;
        }
        return;
    }

    out_primary = TaskPrimaryCategory::Work;
    if (text.find("debug") != std::string::npos || text.find("gdb") != std::string::npos) {
        out_secondary = TaskSecondaryCategory::WorkDebugging;
    } else if (text.find("readme") != std::string::npos || text.find("docs") != std::string::npos || text.find("wiki") != std::string::npos) {
        out_secondary = TaskSecondaryCategory::WorkReadingDocs;
    } else if (text.find("meeting") != std::string::npos || text.find("minutes") != std::string::npos) {
        out_secondary = TaskSecondaryCategory::WorkMeetingNotes;
    } else {
        out_secondary = TaskSecondaryCategory::WorkCoding;
    }
}

}  // namespace k2d
