#pragma once

#include <string>

namespace desktoper2D {

inline constexpr const char *kOverviewWindowName = "Runtime Overview";
inline constexpr const char *kEditorWindowName = "Runtime Editor";
inline constexpr const char *kTimelineWindowName = "Runtime Timeline";
inline constexpr const char *kPerceptionWindowName = "Runtime Perception";
inline constexpr const char *kMappingWindowName = "Runtime Mapping";
inline constexpr const char *kAsrWindowName = "Runtime ASR";
inline constexpr const char *kChatWindowName = "Runtime Chat";
inline constexpr const char *kErrorWindowName = "Runtime Errors";
inline constexpr const char *kInspectorWindowName = "Model Hierarchy + Inspector";
inline constexpr const char *kReminderWindowName = "Reminder";
inline constexpr const char *kWorkspaceDockspaceName = "Runtime Workspace DockSpace";

struct WorkspaceWindowVisibility {
    bool show_workspace_window = true;
    bool show_overview_window = true;
    bool show_editor_window = true;
    bool show_timeline_window = true;
    bool show_perception_window = true;
    bool show_mapping_window = true;
    bool show_asr_chat_window = true;
    bool show_chat_window = true;
    bool show_error_window = true;
    bool show_inspector_window = true;
    bool show_reminder_window = true;
};

}  // namespace desktoper2D
