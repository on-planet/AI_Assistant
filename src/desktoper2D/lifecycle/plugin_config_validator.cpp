#include "desktoper2D/lifecycle/plugin_config_validator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace desktoper2D {

bool JsonArrayAllString(const JsonValue *v) {
    if (!v || !v->isArray() || !v->asArray()) return false;
    for (const auto &item : *v->asArray()) {
        if (!item.isString()) return false;
    }
    return true;
}

std::string ReadTextFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::string Fnv1a64OfFileHex(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};

    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;

    char buf[8192];
    while (ifs) {
        ifs.read(buf, sizeof(buf));
        const std::streamsize n = ifs.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buf[i]);
            hash *= kPrime;
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}

void ApplyWorkerTuningFromJsonObject(const JsonValue &worker, PluginArtifactSpec::WorkerTuning &cfg) {
    if (!worker.isObject() || !worker.asObject()) return;
    const auto &obj = *worker.asObject();

    auto read_int = [&](const char *key, int &out) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            if (const double *v = it->second.asNumber()) {
                out = static_cast<int>(*v);
            }
        }
    };

    auto read_size = [&](const char *key, std::size_t &out) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            if (const double *v = it->second.asNumber()) {
                if (*v >= 0.0) {
                    out = static_cast<std::size_t>(*v);
                }
            }
        }
    };

    auto read_double = [&](const char *key, double &out) {
        auto it = obj.find(key);
        if (it != obj.end()) {
            if (const double *v = it->second.asNumber()) {
                out = *v;
            }
        }
    };

    read_int("update_hz", cfg.update_hz);
    read_int("frame_budget_ms", cfg.frame_budget_ms);
    read_int("timeout_degrade_threshold", cfg.timeout_degrade_threshold);
    read_int("recover_after_consecutive_successes", cfg.recover_after_consecutive_successes);
    read_double("avg_latency_budget_ms", cfg.avg_latency_budget_ms);
    read_size("latency_budget_window_size", cfg.latency_budget_window_size);
    read_int("disable_after_consecutive_failures", cfg.disable_after_consecutive_failures);
    read_int("auto_recover_after_ms", cfg.auto_recover_after_ms);

    auto it = obj.find("degrade_hz_steps");
    if (it != obj.end() && it->second.isArray() && it->second.asArray()) {
        cfg.degrade_hz_steps.clear();
        for (const auto &v : *it->second.asArray()) {
            if (const double *n = v.asNumber()) {
                cfg.degrade_hz_steps.push_back(static_cast<int>(*n));
            }
        }
    }
}

static std::vector<std::string> ReadStringArray(const JsonValue *v) {
    std::vector<std::string> out;
    if (!v || !v->isArray() || !v->asArray()) return out;
    for (const auto &item : *v->asArray()) {
        if (const std::string *s = item.asString()) {
            out.push_back(*s);
        }
    }
    return out;
}

bool ValidatePluginConfig(const JsonValue &root,
                          const std::string &config_path,
                          PluginArtifactSpec *out_spec,
                          std::string *out_error) {
    if (!out_spec) {
        if (out_error) *out_error = "out_spec is null";
        return false;
    }
    if (!root.isObject() || !root.asObject()) {
        if (out_error) *out_error = "plugin config root is not object";
        return false;
    }

    const auto &obj = *root.asObject();
    PluginArtifactSpec spec{};
    spec.config_path = config_path;

    if (auto it = obj.find("model_id"); it != obj.end()) {
        if (const std::string *v = it->second.asString()) spec.model_id = *v;
    }
    if (auto it = obj.find("model_version"); it != obj.end()) {
        if (const std::string *v = it->second.asString()) spec.model_version = *v;
    }
    if (auto it = obj.find("onnx"); it != obj.end()) {
        if (const std::string *v = it->second.asString()) spec.onnx_path = *v;
    }
    if (auto it = obj.find("onnx_checksum_fnv1a64"); it != obj.end()) {
        if (const std::string *v = it->second.asString()) spec.onnx_checksum_fnv1a64 = *v;
    }

    if (spec.onnx_path.empty()) {
        if (out_error) *out_error = "plugin config missing onnx";
        return false;
    }

    const std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
    if (!spec.onnx_path.empty()) {
        std::filesystem::path onnx_path = std::filesystem::path(spec.onnx_path);
        if (onnx_path.is_relative()) {
            spec.onnx_path = (config_dir / onnx_path).lexically_normal().generic_string();
        }
    }

    if (auto it = obj.find("backend_priority"); it != obj.end()) {
        spec.backend_priority = ReadStringArray(&it->second);
    }
    if (spec.backend_priority.empty()) {
        spec.backend_priority.push_back("cpu");
    }

    if (auto it = obj.find("extra_onnx"); it != obj.end()) {
        spec.extra_onnx_paths = ReadStringArray(&it->second);
    }
    if (!spec.extra_onnx_paths.empty()) {
        for (auto &path_str : spec.extra_onnx_paths) {
            std::filesystem::path p = std::filesystem::path(path_str);
            if (p.is_relative()) {
                path_str = (config_dir / p).lexically_normal().generic_string();
            }
        }
    }

    if (auto it = obj.find("io_contract"); it != obj.end() && it->second.isObject() && it->second.asObject()) {
        const auto &io = *it->second.asObject();
        if (auto it2 = io.find("inputs"); it2 != io.end()) {
            auto inputs = ReadStringArray(&it2->second);
            if (!inputs.empty()) spec.tensor_schema.input_name = inputs.front();
        }
        if (auto it2 = io.find("outputs"); it2 != io.end()) {
            auto outputs = ReadStringArray(&it2->second);
            if (!outputs.empty()) spec.tensor_schema.output_name = outputs.front();
        }
    }

    if (auto it = obj.find("postprocess"); it != obj.end() && it->second.isObject() && it->second.asObject()) {
        const auto &pp = *it->second.asObject();
        if (auto steps_it = pp.find("steps"); steps_it != pp.end()) {
            if (steps_it->second.isArray() && steps_it->second.asArray()) {
                for (const auto &step_val : *steps_it->second.asArray()) {
                    if (!step_val.isObject() || !step_val.asObject()) continue;
                    const auto &step_obj = *step_val.asObject();
                    PluginPostprocessStep step{};
                    if (auto t = step_obj.find("type"); t != step_obj.end()) {
                        if (const std::string *s = t->second.asString()) {
                            if (*s == "normalize") step.type = PluginPostprocessStepType::Normalize;
                            else if (*s == "tokenize") step.type = PluginPostprocessStepType::Tokenize;
                            else if (*s == "argmax") step.type = PluginPostprocessStepType::Argmax;
                            else if (*s == "threshold") step.type = PluginPostprocessStepType::Threshold;
                        }
                    }
                    if (auto n = step_obj.find("name"); n != step_obj.end()) {
                        if (const std::string *s = n->second.asString()) step.name = *s;
                    }
                    if (auto e = step_obj.find("epsilon"); e != step_obj.end()) {
                        if (const double *v = e->second.asNumber()) step.epsilon = static_cast<float>(*v);
                    }
                    if (auto d = step_obj.find("delimiter"); d != step_obj.end()) {
                        if (const std::string *s = d->second.asString()) step.delimiter = *s;
                    }
                    if (auto th = step_obj.find("threshold"); th != step_obj.end()) {
                        if (const double *v = th->second.asNumber()) step.threshold = static_cast<float>(*v);
                    }
                    spec.postprocess.steps.push_back(std::move(step));
                }
            }
        }
    }

    if (auto it = obj.find("routing"); it != obj.end() && it->second.isObject() && it->second.asObject()) {
        const auto &routing = *it->second.asObject();
        if (auto d = routing.find("default_route"); d != routing.end()) {
            if (const std::string *s = d->second.asString()) spec.route_config.default_route = *s;
        }
        if (auto rules_it = routing.find("rules"); rules_it != routing.end() && rules_it->second.isArray() && rules_it->second.asArray()) {
            for (const auto &rule_val : *rules_it->second.asArray()) {
                if (!rule_val.isObject() || !rule_val.asObject()) continue;
                const auto &rule_obj = *rule_val.asObject();
                PluginRouteRule rule{};
                if (auto n = rule_obj.find("name"); n != rule_obj.end()) {
                    if (const std::string *s = n->second.asString()) rule.name = *s;
                }
                if (auto s = rule_obj.find("scene_keywords"); s != rule_obj.end()) rule.scene_keywords = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("task_keywords"); s != rule_obj.end()) rule.task_keywords = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("scene_negative_keywords"); s != rule_obj.end()) rule.scene_negative_keywords = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("task_negative_keywords"); s != rule_obj.end()) rule.task_negative_keywords = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("primary_labels"); s != rule_obj.end()) rule.primary_labels = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("secondary_labels"); s != rule_obj.end()) rule.secondary_labels = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("primary_negative_labels"); s != rule_obj.end()) rule.primary_negative_labels = ReadStringArray(&s->second);
                if (auto s = rule_obj.find("secondary_negative_labels"); s != rule_obj.end()) rule.secondary_negative_labels = ReadStringArray(&s->second);
                if (auto o = rule_obj.find("opacity_bias"); o != rule_obj.end()) {
                    if (const double *v = o->second.asNumber()) rule.opacity_bias = static_cast<float>(*v);
                }
                spec.route_config.rules.push_back(std::move(rule));
            }
        }
    }

    if (auto it = obj.find("worker_tuning"); it != obj.end()) {
        ApplyWorkerTuningFromJsonObject(it->second, spec.worker_tuning);
    }

    *out_spec = std::move(spec);
    if (out_error) out_error->clear();
    return true;
}

}  // namespace desktoper2D
