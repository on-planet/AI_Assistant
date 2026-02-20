#pragma once

#include <SDL3/SDL.h>

#include "k2d/core/model.h"

#include <filesystem>
#include <string>

namespace k2d {

struct ModelReloadServiceContext {
    bool dev_hot_reload_enabled = false;
    float *hot_reload_poll_accum_sec = nullptr;

    bool *model_loaded = nullptr;
    ModelRuntime *model = nullptr;
    SDL_Renderer *renderer = nullptr;

    float *model_time = nullptr;
    int *selected_part_index = nullptr;

    std::filesystem::file_time_type *model_last_write_time = nullptr;
    bool *model_last_write_time_valid = nullptr;

    void (*ensure_selected_part_index_valid)(void *user_data) = nullptr;
    void *ensure_selected_part_index_valid_user_data = nullptr;

    void (*ensure_selected_param_index_valid)(void *user_data) = nullptr;
    void *ensure_selected_param_index_valid_user_data = nullptr;

    void (*sync_animation_channel_state)(void *user_data) = nullptr;
    void *sync_animation_channel_state_user_data = nullptr;

    void (*set_editor_status)(const std::string &text, float ttl_sec, void *user_data) = nullptr;
    void *set_editor_status_user_data = nullptr;
};

std::string BuildStableModelBackupPath(const std::string &model_path);
void CommitStableModelBackup(const ModelRuntime &model);
void TryHotReloadModel(ModelReloadServiceContext &ctx, float dt_sec);

}  // namespace k2d
