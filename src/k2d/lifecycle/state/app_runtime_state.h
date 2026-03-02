#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_tray.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "k2d/controllers/app_bootstrap.h"
#include "k2d/controllers/interaction_controller.h"
#include "k2d/core/model.h"
#include "k2d/editor/editor_commands.h"
#include "k2d/editor/editor_controller.h"
#include "k2d/editor/editor_gizmo.h"
#include "k2d/lifecycle/inference_adapter.h"
#include "k2d/lifecycle/perception_pipeline.h"
#include "k2d/lifecycle/plugin_lifecycle.h"
#include "k2d/lifecycle/reminder_service.h"

namespace k2d {

enum class AxisConstraint {
    None,
    XOnly,
    YOnly,
};

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

enum class EditorProp {
    PosX,
    PosY,
    PivotX,
    PivotY,
    RotDeg,
    ScaleX,
    ScaleY,
    Count,
};

struct AppRuntime {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Tray *tray = nullptr;
    SDL_TrayEntry *entry_click_through = nullptr;
    SDL_TrayEntry *entry_show_hide = nullptr;

    SDL_Texture *demo_texture = nullptr;
    int demo_texture_w = 0;
    int demo_texture_h = 0;

    ModelRuntime model;
    bool model_loaded = false;
    float model_time = 0.0f;

    bool running = true;

    bool dev_hot_reload_enabled = true;
    float hot_reload_poll_accum_sec = 0.0f;
    std::filesystem::file_time_type model_last_write_time{};
    bool model_last_write_time_valid = false;
    bool click_through = false;
    bool window_visible = true;
    int window_w = 0;
    int window_h = 0;
    SDL_Rect interactive_rect{0, 0, 0, 0};

    bool show_debug_stats = true;
    bool manual_param_mode = false;
    int selected_param_index = 0;

    bool edit_mode = false;
    int selected_part_index = -1;
    bool dragging_part = false;
    bool dragging_pivot = false;
    float drag_last_x = 0.0f;
    float drag_last_y = 0.0f;
    float drag_start_pos_x = 0.0f;
    float drag_start_pos_y = 0.0f;
    float drag_start_pivot_x = 0.0f;
    float drag_start_pivot_y = 0.0f;

    AxisConstraint axis_constraint = AxisConstraint::None;
    bool snap_enabled = false;
    float snap_grid = 10.0f;

    bool dragging_model_whole = false;
    float dragging_model_last_x = 0.0f;
    float dragging_model_last_y = 0.0f;

    bool property_panel_enabled = true;
    int selected_editor_prop = 0;

    std::vector<EditCommand> undo_stack;
    std::vector<EditCommand> redo_stack;

    GizmoHandle gizmo_hover_handle = GizmoHandle::None;
    GizmoHandle gizmo_active_handle = GizmoHandle::None;
    bool gizmo_dragging = false;
    float gizmo_drag_start_mouse_x = 0.0f;
    float gizmo_drag_start_mouse_y = 0.0f;
    float gizmo_drag_start_pos_x = 0.0f;
    float gizmo_drag_start_pos_y = 0.0f;
    float gizmo_drag_start_rot_deg = 0.0f;
    float gizmo_drag_start_scale_x = 1.0f;
    float gizmo_drag_start_scale_y = 1.0f;
    float gizmo_drag_start_angle = 0.0f;
    float gizmo_drag_start_dist = 1.0f;

    bool edit_capture_active = false;
    EditCommand active_edit_cmd;

    std::string editor_status;
    float editor_status_ttl = 0.0f;

    float debug_fps = 0.0f;
    float debug_frame_ms = 0.0f;
    float debug_fps_accum_sec = 0.0f;
    int debug_fps_accum_frames = 0;

    bool gui_enabled = true;

    InteractionControllerState interaction_state{};

    std::unique_ptr<IInferenceAdapter> inference_adapter;
    bool plugin_ready = false;
    PluginParamBlendMode plugin_param_blend_mode = PluginParamBlendMode::Override;

    ReminderService reminder_service;
    bool reminder_ready = false;
    float reminder_poll_accum_sec = 0.0f;
    char reminder_title_input[128] = "喝水";
    int reminder_after_min = 10;
    std::vector<ReminderItem> reminder_upcoming;
    std::string reminder_last_error;

    PerceptionPipeline perception_pipeline;
    PerceptionPipelineState perception_state;

    TaskPrimaryCategory task_primary = TaskPrimaryCategory::Unknown;
    TaskSecondaryCategory task_secondary = TaskSecondaryCategory::Unknown;
};

extern AppRuntime g_runtime;
extern EditorControllerState g_editor_state;

}  // namespace k2d
