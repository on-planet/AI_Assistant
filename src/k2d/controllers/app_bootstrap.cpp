#include "k2d/controllers/app_bootstrap.h"

#include <SDL3/SDL.h>

#include "k2d/core/json.h"
#include "k2d/core/png_texture.h"

#include <algorithm>

namespace k2d {
namespace {

AppRuntimeConfig BuildSafeRuntimeConfig() {
    AppRuntimeConfig cfg;
    cfg.window_width = 800;
    cfg.window_height = 600;
    cfg.window_opacity = 0.5f;
    cfg.click_through = false;
    cfg.window_visible = true;

    cfg.show_debug_stats = true;
    cfg.manual_param_mode = false;
    cfg.dev_hot_reload_enabled = true;

    cfg.default_model_candidates = {
        "assets/model_01/model.json",
        "../assets/model_01/model.json",
        "../../assets/model_01/model.json",
    };
    return cfg;
}

AppRuntimeConfig LoadRuntimeConfigImpl() {
    AppRuntimeConfig cfg = BuildSafeRuntimeConfig();

    const char *config_candidates[] = {
        "assets/app_config.json",
        "../assets/app_config.json",
        "../../assets/app_config.json",
    };

    std::string text;
    for (const char *path : config_candidates) {
        SDL_IOStream *io = SDL_IOFromFile(path, "rb");
        if (!io) continue;
        const Sint64 sz = SDL_GetIOSize(io);
        if (sz > 0) {
            text.resize(static_cast<std::size_t>(sz));
            const size_t got = SDL_ReadIO(io, text.data(), text.size());
            SDL_CloseIO(io);
            if (got == text.size()) {
                break;
            }
            text.clear();
            continue;
        }
        SDL_CloseIO(io);
    }

    if (text.empty()) {
        SDL_Log("Runtime config not found, using safe defaults");
        return cfg;
    }

    JsonParseError err;
    auto root_opt = ParseJson(text, &err);
    if (!root_opt || !root_opt->isObject()) {
        SDL_Log("Runtime config parse failed at %zu:%zu (%s), using safe defaults",
                err.line,
                err.column,
                err.message.c_str());
        return cfg;
    }

    const JsonValue &root = *root_opt;
    if (const JsonValue *w = root.get("window"); w && w->isObject()) {
        cfg.window_width = static_cast<int>(w->getNumber("width").value_or(cfg.window_width));
        cfg.window_height = static_cast<int>(w->getNumber("height").value_or(cfg.window_height));
        cfg.window_opacity = static_cast<float>(w->getNumber("opacity").value_or(cfg.window_opacity));
        cfg.click_through = w->getBool("clickThrough").value_or(cfg.click_through);
        cfg.window_visible = w->getBool("visible").value_or(cfg.window_visible);
    }

    if (const JsonValue *debug = root.get("debug"); debug && debug->isObject()) {
        cfg.show_debug_stats = debug->getBool("showStats").value_or(cfg.show_debug_stats);
        cfg.dev_hot_reload_enabled = debug->getBool("hotReload").value_or(cfg.dev_hot_reload_enabled);
    }

    if (const JsonValue *startup = root.get("startup"); startup && startup->isObject()) {
        cfg.manual_param_mode = startup->getBool("manualParamMode").value_or(cfg.manual_param_mode);
        if (auto model = startup->getString("defaultModel"); model.has_value() && !model->empty()) {
            cfg.default_model_candidates.insert(cfg.default_model_candidates.begin(), *model);
        }
    }

    cfg.window_width = std::max(64, cfg.window_width);
    cfg.window_height = std::max(64, cfg.window_height);
    cfg.window_opacity = std::clamp(cfg.window_opacity, 0.05f, 1.0f);
    if (cfg.default_model_candidates.empty()) {
        cfg.default_model_candidates = BuildSafeRuntimeConfig().default_model_candidates;
    }

    return cfg;
}

}  // namespace

AppRuntimeConfig LoadRuntimeConfig() {
    return LoadRuntimeConfigImpl();
}

AppBootstrapResult BootstrapModelAndResources(SDL_Renderer *renderer) {
    AppBootstrapResult result{};
    result.runtime_config = LoadRuntimeConfig();

    std::string model_err;
    ModelRuntime model;
    std::string model_attempt_errors;
    for (const std::string &candidate : result.runtime_config.default_model_candidates) {
        model_err.clear();
        result.model_loaded = LoadModelRuntime(renderer, candidate.c_str(), &model, &model_err);
        if (result.model_loaded) {
            result.model_load_log = std::string("Model loaded: ") + candidate;
            break;
        }

        model_attempt_errors += "candidate='";
        model_attempt_errors += candidate;
        model_attempt_errors += "' => ";
        model_attempt_errors += model_err;
        model_attempt_errors += "\n";
    }

    if (!result.model_loaded) {
        result.model_load_log = std::string("LoadModelRuntime failed (all candidates). details:\n") + model_attempt_errors;
    } else {
        result.model = std::move(model);
    }

    result.demo_texture = LoadPngTexture(renderer,
                                         "test.png",
                                         &result.demo_texture_w,
                                         &result.demo_texture_h,
                                         &result.demo_texture_error);

    return result;
}

}  // namespace k2d


