#include "desktoper2D/lifecycle/model_reload_service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace desktoper2D {

std::string BuildStableModelBackupPath(const std::string &model_path) {
    if (model_path.empty()) {
        return "assets/model_01/model.last_good.json";
    }
    return model_path + ".last_good.json";
}

void CommitStableModelBackup(const ModelRuntime &model) {
    if (model.model_path.empty()) {
        return;
    }
    std::string save_err;
    const std::string backup_path = BuildStableModelBackupPath(model.model_path);
    if (!SaveModelRuntimeJson(model, backup_path.c_str(), &save_err)) {
        SDL_Log("Stable model backup save failed: %s", save_err.c_str());
    }
}

void TryHotReloadModel(ModelReloadServiceContext &ctx, float dt_sec) {
    if (!ctx.dev_hot_reload_enabled || !ctx.model_loaded || !ctx.model || !(*ctx.model_loaded)) {
        return;
    }
    if (ctx.model->model_path.empty()) {
        return;
    }
    if (!ctx.hot_reload_poll_accum_sec || !ctx.renderer || !ctx.model_time || !ctx.selected_part_index ||
        !ctx.model_last_write_time || !ctx.model_last_write_time_valid) {
        return;
    }

    *ctx.hot_reload_poll_accum_sec += std::max(0.0f, dt_sec);
    if (*ctx.hot_reload_poll_accum_sec < 0.5f) {
        return;
    }
    *ctx.hot_reload_poll_accum_sec = 0.0f;

    std::error_code ec;
    const auto now_write_time = std::filesystem::last_write_time(ctx.model->model_path, ec);
    if (ec) {
        return;
    }

    if (!(*ctx.model_last_write_time_valid)) {
        *ctx.model_last_write_time = now_write_time;
        *ctx.model_last_write_time_valid = true;
        return;
    }

    if (now_write_time == *ctx.model_last_write_time) {
        return;
    }

    ModelRuntime new_model;
    std::string load_err;
    if (!LoadModelRuntime(ctx.renderer, ctx.model->model_path.c_str(), &new_model, &load_err)) {
        const std::string backup_path = BuildStableModelBackupPath(ctx.model->model_path);
        ModelRuntime rollback_model;
        std::string rollback_err;
        if (LoadModelRuntime(ctx.renderer, backup_path.c_str(), &rollback_model, &rollback_err)) {
            DestroyModelRuntime(ctx.model);
            *ctx.model = std::move(rollback_model);
            *ctx.model_loaded = true;
            *ctx.model_time = 0.0f;
            *ctx.selected_part_index = -1;

            if (ctx.ensure_selected_part_index_valid) {
                ctx.ensure_selected_part_index_valid(ctx.ensure_selected_part_index_valid_user_data);
            }
            if (ctx.ensure_selected_param_index_valid) {
                ctx.ensure_selected_param_index_valid(ctx.ensure_selected_param_index_valid_user_data);
            }
            if (ctx.sync_animation_channel_state) {
                ctx.sync_animation_channel_state(ctx.sync_animation_channel_state_user_data);
            }
            if (ctx.set_editor_status) {
                ctx.set_editor_status("hot reload failed, rolled back to last stable model", 3.0f,
                                      ctx.set_editor_status_user_data);
            }
            SDL_Log("Hot reload failed: %s | rollback applied from %s", load_err.c_str(), backup_path.c_str());
        } else {
            if (ctx.set_editor_status) {
                ctx.set_editor_status(std::string("hot reload failed: ") + load_err, 3.0f,
                                      ctx.set_editor_status_user_data);
            }
            SDL_Log("Hot reload failed and rollback unavailable: reload_err=%s rollback_err=%s",
                    load_err.c_str(),
                    rollback_err.c_str());
        }
        return;
    }

    DestroyModelRuntime(ctx.model);
    *ctx.model = std::move(new_model);
    *ctx.model_loaded = true;
    *ctx.model_time = 0.0f;
    *ctx.model_last_write_time = now_write_time;
    *ctx.model_last_write_time_valid = true;

    *ctx.selected_part_index = -1;
    if (ctx.ensure_selected_part_index_valid) {
        ctx.ensure_selected_part_index_valid(ctx.ensure_selected_part_index_valid_user_data);
    }
    if (ctx.ensure_selected_param_index_valid) {
        ctx.ensure_selected_param_index_valid(ctx.ensure_selected_param_index_valid_user_data);
    }
    if (ctx.sync_animation_channel_state) {
        ctx.sync_animation_channel_state(ctx.sync_animation_channel_state_user_data);
    }

    CommitStableModelBackup(*ctx.model);
    if (ctx.set_editor_status) {
        ctx.set_editor_status("model hot reloaded", 1.5f, ctx.set_editor_status_user_data);
    }
}

}  // namespace desktoper2D
