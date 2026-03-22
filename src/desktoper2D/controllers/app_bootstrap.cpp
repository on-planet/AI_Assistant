#include "desktoper2D/controllers/app_bootstrap.h"

#include <SDL3/SDL.h>

#include "desktoper2D/core/json.h"
#include "desktoper2D/core/png_texture.h"

#include <algorithm>

namespace desktoper2D {
namespace {

void LoadTaskCategoryKeywords(const JsonValue *arr, std::vector<std::string> &out) {
    if (!arr || !arr->isArray()) {
        return;
    }
    const auto *items = arr->asArray();
    if (!items) {
        return;
    }

    std::vector<std::string> values;
    values.reserve(items->size());
    for (const auto &item : *items) {
        if (!item.isString()) {
            continue;
        }
        const std::string *s = item.asString();
        if (!s || s->empty()) {
            continue;
        }
        values.push_back(*s);
    }

    if (!values.empty()) {
        out = std::move(values);
    }
}

TaskPrimaryCategory ParsePrimaryCategory(const std::string &name) {
    if (name == "work") return TaskPrimaryCategory::Work;
    if (name == "game") return TaskPrimaryCategory::Game;
    return TaskPrimaryCategory::Unknown;
}

TaskSecondaryCategory ParseSecondaryCategory(const std::string &name) {
    if (name == "coding") return TaskSecondaryCategory::WorkCoding;
    if (name == "debugging") return TaskSecondaryCategory::WorkDebugging;
    if (name == "reading_docs") return TaskSecondaryCategory::WorkReadingDocs;
    if (name == "meeting_notes") return TaskSecondaryCategory::WorkMeetingNotes;
    if (name == "lobby") return TaskSecondaryCategory::GameLobby;
    if (name == "match") return TaskSecondaryCategory::GameMatch;
    if (name == "settlement") return TaskSecondaryCategory::GameSettlement;
    if (name == "menu") return TaskSecondaryCategory::GameMenu;
    return TaskSecondaryCategory::Unknown;
}

void ApplyFusionConfigFromJsonObject(const JsonValue &fusion, BehaviorFusionConfig &cfg) {
    const std::string fusion_mode = fusion.getString("mode").value_or(std::string());
    if (fusion_mode == "priority_override") {
        cfg.mode = BehaviorFusionMode::PriorityOverride;
    } else if (fusion_mode == "weighted_average") {
        cfg.mode = BehaviorFusionMode::WeightedAverage;
    }

    cfg.local_weight = static_cast<float>(fusion.getNumber("localWeight").value_or(cfg.local_weight));
    cfg.plugin_weight = static_cast<float>(fusion.getNumber("pluginWeight").value_or(cfg.plugin_weight));
    cfg.normalize_by_weight_sum = fusion.getBool("normalizeByWeightSum").value_or(cfg.normalize_by_weight_sum);
}

void TryApplyPluginBehaviorFusionConfig(BehaviorFusionConfig &cfg) {
    const char *config_candidates[] = {
        "assets/plugin_behavior_config.json",
        "../assets/plugin_behavior_config.json",
        "../../assets/plugin_behavior_config.json",
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
        return;
    }

    JsonParseError err;
    auto root_opt = ParseJson(text, &err);
    if (!root_opt || !root_opt->isObject()) {
        return;
    }

    if (const JsonValue *fusion = root_opt->get("fusion"); fusion && fusion->isObject()) {
        ApplyFusionConfigFromJsonObject(*fusion, cfg);
    }
}

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
    cfg.plugin_param_blend_mode = PluginParamBlendMode::Override;
    cfg.behavior_fusion.mode = BehaviorFusionMode::WeightedAverage;
    cfg.behavior_fusion.local_weight = 1.0f;
    cfg.behavior_fusion.plugin_weight = 1.0f;
    cfg.behavior_fusion.normalize_by_weight_sum = true;

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

    if (const JsonValue *plugin = root.get("plugin"); plugin && plugin->isObject()) {
        const std::string mode = plugin->getString("paramBlendMode").value_or(std::string());
        if (mode == "weighted") {
            cfg.plugin_param_blend_mode = PluginParamBlendMode::Weighted;
        } else if (mode == "override") {
            cfg.plugin_param_blend_mode = PluginParamBlendMode::Override;
        }

        if (const JsonValue *fusion = plugin->get("fusion"); fusion && fusion->isObject()) {
            const std::string fusion_mode = fusion->getString("mode").value_or(std::string());
            if (fusion_mode == "priority_override") {
                cfg.behavior_fusion.mode = BehaviorFusionMode::PriorityOverride;
            } else if (fusion_mode == "weighted_average") {
                cfg.behavior_fusion.mode = BehaviorFusionMode::WeightedAverage;
            }

            cfg.behavior_fusion.local_weight =
                static_cast<float>(fusion->getNumber("localWeight").value_or(cfg.behavior_fusion.local_weight));
            cfg.behavior_fusion.plugin_weight =
                static_cast<float>(fusion->getNumber("pluginWeight").value_or(cfg.behavior_fusion.plugin_weight));
            cfg.behavior_fusion.normalize_by_weight_sum =
                fusion->getBool("normalizeByWeightSum").value_or(cfg.behavior_fusion.normalize_by_weight_sum);
        }
    }

    if (const JsonValue *startup = root.get("startup"); startup && startup->isObject()) {
        cfg.manual_param_mode = startup->getBool("manualParamMode").value_or(cfg.manual_param_mode);
        if (auto model = startup->getString("defaultModel"); model.has_value() && !model->empty()) {
            cfg.default_model_candidates.insert(cfg.default_model_candidates.begin(), *model);
        }
    }

    if (const JsonValue *face_mapping = root.get("faceMapping"); face_mapping && face_mapping->isObject()) {
        if (const JsonValue *sensor_fallback = face_mapping->get("sensorFallbackTemplate");
            sensor_fallback && sensor_fallback->isObject()) {
            cfg.face_map_sensor_fallback_enabled =
                sensor_fallback->getBool("enabled").value_or(cfg.face_map_sensor_fallback_enabled);
            cfg.face_map_sensor_fallback_head_yaw =
                static_cast<float>(sensor_fallback->getNumber("headYaw").value_or(cfg.face_map_sensor_fallback_head_yaw));
            cfg.face_map_sensor_fallback_head_pitch =
                static_cast<float>(sensor_fallback->getNumber("headPitch").value_or(cfg.face_map_sensor_fallback_head_pitch));
            cfg.face_map_sensor_fallback_eye_open =
                static_cast<float>(sensor_fallback->getNumber("eyeOpen").value_or(cfg.face_map_sensor_fallback_eye_open));
            cfg.face_map_sensor_fallback_weight =
                static_cast<float>(sensor_fallback->getNumber("weight").value_or(cfg.face_map_sensor_fallback_weight));
        }
    }

    if (const JsonValue *task_category = root.get("taskCategory"); task_category && task_category->isObject()) {
        const auto load_task_float = [](const JsonValue *obj, const char *key, float &out) {
            if (!obj || !obj->isObject()) return;
            out = static_cast<float>(obj->getNumber(key).value_or(out));
        };
        const auto load_task_int = [](const JsonValue *obj, const char *key, int &out) {
            if (!obj || !obj->isObject()) return;
            if (const auto value = obj->getNumber(key)) {
                out = static_cast<int>(*value);
            }
        };
        const auto load_task_size = [](const JsonValue *obj, const char *key, std::size_t &out) {
            if (!obj || !obj->isObject()) return;
            if (const auto value = obj->getNumber(key)) {
                out = static_cast<std::size_t>(std::max(0.0, *value));
            }
        };

        if (const JsonValue *game_primary_keywords = task_category->get("gamePrimaryKeywords")) {
            LoadTaskCategoryKeywords(game_primary_keywords, cfg.task_category.game_primary_keywords);
        }
        if (const JsonValue *work_primary_keywords = task_category->get("workPrimaryKeywords")) {
            LoadTaskCategoryKeywords(work_primary_keywords, cfg.task_category.work_primary_keywords);
        }

        if (const JsonValue *default_work = task_category->get("defaultWorkSecondary");
            default_work && default_work->isString() && default_work->asString()) {
            const TaskSecondaryCategory parsed = ParseSecondaryCategory(*default_work->asString());
            if (parsed != TaskSecondaryCategory::Unknown) {
                cfg.task_category.default_work_secondary = parsed;
            }
        }

        if (const JsonValue *default_game = task_category->get("defaultGameSecondary");
            default_game && default_game->isString() && default_game->asString()) {
            const TaskSecondaryCategory parsed = ParseSecondaryCategory(*default_game->asString());
            if (parsed != TaskSecondaryCategory::Unknown) {
                cfg.task_category.default_game_secondary = parsed;
            }
        }

        if (const JsonValue *secondary_rules = task_category->get("secondaryRules");
            secondary_rules && secondary_rules->isArray() && secondary_rules->asArray()) {
            std::vector<TaskCategoryKeywordRule> loaded_rules;
            for (const auto &entry : *secondary_rules->asArray()) {
                if (!entry.isObject()) {
                    continue;
                }
                const auto primary_name = entry.getString("primary");
                const auto secondary_name = entry.getString("secondary");
                if (!primary_name || !secondary_name) {
                    continue;
                }

                TaskCategoryKeywordRule rule{};
                rule.primary = ParsePrimaryCategory(*primary_name);
                rule.secondary = ParseSecondaryCategory(*secondary_name);
                if (rule.primary == TaskPrimaryCategory::Unknown ||
                    rule.secondary == TaskSecondaryCategory::Unknown) {
                    continue;
                }

                if (const JsonValue *keywords = entry.get("keywords")) {
                    LoadTaskCategoryKeywords(keywords, rule.keywords);
                }
                if (rule.keywords.empty()) {
                    continue;
                }

                loaded_rules.push_back(std::move(rule));
            }

            if (!loaded_rules.empty()) {
                cfg.task_category.secondary_rules = std::move(loaded_rules);
            }
        }

        if (const JsonValue *calibration = task_category->get("calibration");
            calibration && calibration->isObject()) {
            cfg.task_category.calibration.scene_temperature = static_cast<float>(
                calibration->getNumber("sceneTemperature").value_or(cfg.task_category.calibration.scene_temperature));
            cfg.task_category.calibration.ocr_platt_a = static_cast<float>(
                calibration->getNumber("ocrPlattA").value_or(cfg.task_category.calibration.ocr_platt_a));
            cfg.task_category.calibration.ocr_platt_b = static_cast<float>(
                calibration->getNumber("ocrPlattB").value_or(cfg.task_category.calibration.ocr_platt_b));
            cfg.task_category.calibration.context_temperature = static_cast<float>(
                calibration->getNumber("contextTemperature").value_or(cfg.task_category.calibration.context_temperature));
        }

        if (const JsonValue *primary = task_category->get("primary"); primary && primary->isObject()) {
            load_task_float(primary, "singleSourceSemanticWeight", cfg.task_category.primary.single_source_semantic_weight);
            load_task_float(primary, "singleSourceStructuredWeight", cfg.task_category.primary.single_source_structured_weight);
            load_task_float(primary, "semanticWeight", cfg.task_category.primary.semantic_weight);
            load_task_float(primary, "structuredWeight", cfg.task_category.primary.structured_weight);
            load_task_float(primary, "explicitWeight", cfg.task_category.primary.explicit_weight);
            load_task_float(primary, "ocrStructWeight", cfg.task_category.primary.ocr_struct_weight);
            load_task_float(primary, "explicitGameNegationBias", cfg.task_category.primary.explicit_game_negation_bias);
            load_task_float(primary, "explicitGameConditionalBias", cfg.task_category.primary.explicit_game_conditional_bias);
            load_task_float(primary, "explicitGameMultiIntentBias", cfg.task_category.primary.explicit_game_multi_intent_bias);
            load_task_float(primary, "explicitWorkNegationBias", cfg.task_category.primary.explicit_work_negation_bias);
            load_task_float(primary, "explicitWorkConditionalBias", cfg.task_category.primary.explicit_work_conditional_bias);
            load_task_float(primary, "explicitWorkMultiIntentBias", cfg.task_category.primary.explicit_work_multi_intent_bias);
            load_task_float(primary, "gameOcrUiWeight", cfg.task_category.primary.game_ocr_ui_weight);
            load_task_float(primary, "workOcrCodeWeight", cfg.task_category.primary.work_ocr_code_weight);
            load_task_float(primary, "workOcrOfficeWeight", cfg.task_category.primary.work_ocr_office_weight);
            load_task_float(primary, "workOcrChatWeight", cfg.task_category.primary.work_ocr_chat_weight);
            load_task_float(primary, "confidenceConsistencyBias", cfg.task_category.primary.confidence_consistency_bias);
            load_task_float(primary, "confidenceConsistencyWeight", cfg.task_category.primary.confidence_consistency_weight);
            load_task_float(primary, "confidenceTemperature", cfg.task_category.primary.confidence_temperature);
            load_task_float(primary, "confidencePlattA", cfg.task_category.primary.confidence_platt_a);
            load_task_float(primary, "confidencePlattB", cfg.task_category.primary.confidence_platt_b);
        }

        if (const JsonValue *decision = task_category->get("decision"); decision && decision->isObject()) {
            load_task_size(decision, "asrAssistMaxChars", cfg.task_category.decision.asr_assist_max_chars);
            load_task_float(decision, "primaryConsistencyThreshold", cfg.task_category.decision.primary_consistency_threshold);
            load_task_float(decision, "primaryRejectThreshold", cfg.task_category.decision.primary_reject_threshold);
            load_task_float(decision, "secondaryRejectThreshold", cfg.task_category.decision.secondary_reject_threshold);
            load_task_float(decision, "minReliableSourceWeight", cfg.task_category.decision.min_reliable_source_weight);
            load_task_float(decision, "weakSourcePrimaryRejectThreshold", cfg.task_category.decision.weak_source_primary_reject_threshold);
            load_task_float(decision, "weakSourceSecondaryRejectThreshold", cfg.task_category.decision.weak_source_secondary_reject_threshold);
            load_task_float(decision, "decisionAlpha", cfg.task_category.decision.decision_alpha);
            load_task_int(decision, "rejectHoldFrames", cfg.task_category.decision.reject_hold_frames);
            load_task_int(decision, "secondaryRejectHoldFrames", cfg.task_category.decision.secondary_reject_hold_frames);
            load_task_float(decision, "primaryRejectEmaMargin", cfg.task_category.decision.primary_reject_ema_margin);
            load_task_float(decision, "secondaryRejectEmaMargin", cfg.task_category.decision.secondary_reject_ema_margin);
            load_task_float(decision, "primaryPreemptThreshold", cfg.task_category.decision.primary_preempt_threshold);
            load_task_float(decision, "primaryPreemptMargin", cfg.task_category.decision.primary_preempt_margin);
            load_task_float(decision, "secondaryPreemptThreshold", cfg.task_category.decision.secondary_preempt_threshold);
            load_task_float(decision, "secondaryPreemptMargin", cfg.task_category.decision.secondary_preempt_margin);
            load_task_float(decision, "jointPrimaryConfidenceTemperature", cfg.task_category.decision.joint_primary_confidence_temperature);
            load_task_float(decision, "jointPrimaryConfidencePlattA", cfg.task_category.decision.joint_primary_confidence_platt_a);
            load_task_float(decision, "jointPrimaryConfidencePlattB", cfg.task_category.decision.joint_primary_confidence_platt_b);
            load_task_float(decision, "jointSecondaryConfidenceTemperature", cfg.task_category.decision.joint_secondary_confidence_temperature);
            load_task_float(decision, "jointSecondaryConfidencePlattA", cfg.task_category.decision.joint_secondary_confidence_platt_a);
            load_task_float(decision, "jointSecondaryConfidencePlattB", cfg.task_category.decision.joint_secondary_confidence_platt_b);
        }

        if (const JsonValue *temporal = task_category->get("temporal"); temporal && temporal->isObject()) {
            load_task_float(temporal, "emaAlpha", cfg.task_category.temporal.ema_alpha);
            load_task_float(temporal, "switchMargin", cfg.task_category.temporal.switch_margin);
            load_task_int(temporal, "minHoldFrames", cfg.task_category.temporal.min_hold_frames);
            load_task_float(temporal, "unknownPrimaryKeepThreshold", cfg.task_category.temporal.unknown_primary_keep_threshold);
            load_task_float(temporal, "unknownSecondaryKeepThreshold", cfg.task_category.temporal.unknown_secondary_keep_threshold);
        }

        if (const JsonValue *memory = task_category->get("memory"); memory && memory->isObject()) {
            load_task_float(memory, "primaryBoostMax", cfg.task_category.memory.primary_boost_max);
            load_task_float(memory, "secondaryBoostMax", cfg.task_category.memory.secondary_boost_max);
        }
    }

    TryApplyPluginBehaviorFusionConfig(cfg.behavior_fusion);

    cfg.window_width = std::max(64, cfg.window_width);
    cfg.window_height = std::max(64, cfg.window_height);
    cfg.window_opacity = std::clamp(cfg.window_opacity, 0.05f, 1.0f);
    cfg.face_map_sensor_fallback_head_yaw = std::clamp(cfg.face_map_sensor_fallback_head_yaw, -1.0f, 1.0f);
    cfg.face_map_sensor_fallback_head_pitch = std::clamp(cfg.face_map_sensor_fallback_head_pitch, -1.0f, 1.0f);
    cfg.face_map_sensor_fallback_eye_open = std::clamp(cfg.face_map_sensor_fallback_eye_open, 0.0f, 1.0f);
    cfg.face_map_sensor_fallback_weight = std::clamp(cfg.face_map_sensor_fallback_weight, 0.0f, 1.0f);
    cfg.behavior_fusion.local_weight = std::clamp(cfg.behavior_fusion.local_weight, 0.0f, 1.0f);
    cfg.behavior_fusion.plugin_weight = std::clamp(cfg.behavior_fusion.plugin_weight, 0.0f, 1.0f);
    cfg.task_category.calibration.scene_temperature =
        std::clamp(cfg.task_category.calibration.scene_temperature, 0.05f, 10.0f);
    cfg.task_category.calibration.context_temperature =
        std::clamp(cfg.task_category.calibration.context_temperature, 0.05f, 10.0f);
    cfg.task_category.calibration.ocr_platt_a =
        std::clamp(cfg.task_category.calibration.ocr_platt_a, -10.0f, 10.0f);
    cfg.task_category.calibration.ocr_platt_b =
        std::clamp(cfg.task_category.calibration.ocr_platt_b, -10.0f, 10.0f);
    cfg.task_category.primary.single_source_semantic_weight =
        std::clamp(cfg.task_category.primary.single_source_semantic_weight, 0.0f, 1.0f);
    cfg.task_category.primary.single_source_structured_weight =
        std::clamp(cfg.task_category.primary.single_source_structured_weight, 0.0f, 1.0f);
    cfg.task_category.primary.semantic_weight = std::clamp(cfg.task_category.primary.semantic_weight, 0.0f, 1.0f);
    cfg.task_category.primary.structured_weight = std::clamp(cfg.task_category.primary.structured_weight, 0.0f, 1.0f);
    cfg.task_category.primary.explicit_weight = std::clamp(cfg.task_category.primary.explicit_weight, 0.0f, 1.0f);
    cfg.task_category.primary.ocr_struct_weight = std::clamp(cfg.task_category.primary.ocr_struct_weight, 0.0f, 1.0f);
    cfg.task_category.primary.game_ocr_ui_weight = std::clamp(cfg.task_category.primary.game_ocr_ui_weight, 0.0f, 2.0f);
    cfg.task_category.primary.work_ocr_code_weight = std::clamp(cfg.task_category.primary.work_ocr_code_weight, 0.0f, 2.0f);
    cfg.task_category.primary.work_ocr_office_weight = std::clamp(cfg.task_category.primary.work_ocr_office_weight, 0.0f, 2.0f);
    cfg.task_category.primary.work_ocr_chat_weight = std::clamp(cfg.task_category.primary.work_ocr_chat_weight, 0.0f, 2.0f);
    cfg.task_category.primary.confidence_consistency_bias =
        std::clamp(cfg.task_category.primary.confidence_consistency_bias, 0.0f, 2.0f);
    cfg.task_category.primary.confidence_consistency_weight =
        std::clamp(cfg.task_category.primary.confidence_consistency_weight, 0.0f, 1.0f);
    cfg.task_category.primary.confidence_temperature =
        std::clamp(cfg.task_category.primary.confidence_temperature, 0.05f, 10.0f);
    cfg.task_category.primary.confidence_platt_a =
        std::clamp(cfg.task_category.primary.confidence_platt_a, -10.0f, 10.0f);
    cfg.task_category.primary.confidence_platt_b =
        std::clamp(cfg.task_category.primary.confidence_platt_b, -10.0f, 10.0f);
    cfg.task_category.decision.primary_consistency_threshold =
        std::clamp(cfg.task_category.decision.primary_consistency_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.primary_reject_threshold =
        std::clamp(cfg.task_category.decision.primary_reject_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.secondary_reject_threshold =
        std::clamp(cfg.task_category.decision.secondary_reject_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.min_reliable_source_weight =
        std::clamp(cfg.task_category.decision.min_reliable_source_weight, 0.0f, 1.0f);
    cfg.task_category.decision.weak_source_primary_reject_threshold =
        std::clamp(cfg.task_category.decision.weak_source_primary_reject_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.weak_source_secondary_reject_threshold =
        std::clamp(cfg.task_category.decision.weak_source_secondary_reject_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.decision_alpha =
        std::clamp(cfg.task_category.decision.decision_alpha, 0.0f, 1.0f);
    cfg.task_category.decision.reject_hold_frames = std::max(0, cfg.task_category.decision.reject_hold_frames);
    cfg.task_category.decision.secondary_reject_hold_frames =
        std::max(0, cfg.task_category.decision.secondary_reject_hold_frames);
    cfg.task_category.decision.primary_reject_ema_margin =
        std::clamp(cfg.task_category.decision.primary_reject_ema_margin, 0.0f, 1.0f);
    cfg.task_category.decision.secondary_reject_ema_margin =
        std::clamp(cfg.task_category.decision.secondary_reject_ema_margin, 0.0f, 1.0f);
    cfg.task_category.decision.primary_preempt_threshold =
        std::clamp(cfg.task_category.decision.primary_preempt_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.primary_preempt_margin =
        std::clamp(cfg.task_category.decision.primary_preempt_margin, 0.0f, 1.0f);
    cfg.task_category.decision.secondary_preempt_threshold =
        std::clamp(cfg.task_category.decision.secondary_preempt_threshold, 0.0f, 1.0f);
    cfg.task_category.decision.secondary_preempt_margin =
        std::clamp(cfg.task_category.decision.secondary_preempt_margin, 0.0f, 1.0f);
    cfg.task_category.decision.joint_primary_confidence_temperature =
        std::clamp(cfg.task_category.decision.joint_primary_confidence_temperature, 0.05f, 10.0f);
    cfg.task_category.decision.joint_primary_confidence_platt_a =
        std::clamp(cfg.task_category.decision.joint_primary_confidence_platt_a, -10.0f, 10.0f);
    cfg.task_category.decision.joint_primary_confidence_platt_b =
        std::clamp(cfg.task_category.decision.joint_primary_confidence_platt_b, -10.0f, 10.0f);
    cfg.task_category.decision.joint_secondary_confidence_temperature =
        std::clamp(cfg.task_category.decision.joint_secondary_confidence_temperature, 0.05f, 10.0f);
    cfg.task_category.decision.joint_secondary_confidence_platt_a =
        std::clamp(cfg.task_category.decision.joint_secondary_confidence_platt_a, -10.0f, 10.0f);
    cfg.task_category.decision.joint_secondary_confidence_platt_b =
        std::clamp(cfg.task_category.decision.joint_secondary_confidence_platt_b, -10.0f, 10.0f);
    cfg.task_category.temporal.ema_alpha = std::clamp(cfg.task_category.temporal.ema_alpha, 0.0f, 1.0f);
    cfg.task_category.temporal.switch_margin = std::clamp(cfg.task_category.temporal.switch_margin, 0.0f, 1.0f);
    cfg.task_category.temporal.min_hold_frames = std::max(0, cfg.task_category.temporal.min_hold_frames);
    cfg.task_category.temporal.unknown_primary_keep_threshold =
        std::clamp(cfg.task_category.temporal.unknown_primary_keep_threshold, 0.0f, 1.0f);
    cfg.task_category.temporal.unknown_secondary_keep_threshold =
        std::clamp(cfg.task_category.temporal.unknown_secondary_keep_threshold, 0.0f, 1.0f);
    cfg.task_category.memory.primary_boost_max = std::clamp(cfg.task_category.memory.primary_boost_max, 0.0f, 1.0f);
    cfg.task_category.memory.secondary_boost_max = std::clamp(cfg.task_category.memory.secondary_boost_max, 0.0f, 1.0f);

    if (cfg.default_model_candidates.empty()) {
        cfg.default_model_candidates = BuildSafeRuntimeConfig().default_model_candidates;
    }

    return cfg;
}

}  // namespace

AppRuntimeConfig LoadRuntimeConfig() {
    return LoadRuntimeConfigImpl();
}

AppBootstrapResult BootstrapModelAndResources(const RuntimeWindowState &window_state) {
    AppBootstrapResult result{};
    result.runtime_config = LoadRuntimeConfig();

    std::string model_err;
    ModelRuntime model;
    std::string model_attempt_errors;
    for (const std::string &candidate : result.runtime_config.default_model_candidates) {
        model_err.clear();
        result.model_loaded = LoadModelRuntime(window_state.renderer, candidate.c_str(), &model, &model_err);
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

    result.demo_texture = LoadPngTexture(window_state.renderer,
                                         "test.png",
                                         &result.demo_texture_w,
                                         &result.demo_texture_h,
                                         &result.demo_texture_error);

    return result;
}

}  // namespace desktoper2D


