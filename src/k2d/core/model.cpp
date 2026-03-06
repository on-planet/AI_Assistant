#include "model.h"

#include <SDL3/SDL.h>

#include "json.h"
#include "mesh_generator.h"
#include "png_texture.h"
#include "model_update_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace k2d {

namespace {

static float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

static float ClampRange(float v, float a, float b) {
    const float lo = std::min(a, b);
    const float hi = std::max(a, b);
    return std::clamp(v, lo, hi);
}

static float SafeFinite(float v, float fallback = 0.0f) {
    return std::isfinite(v) ? v : fallback;
}

static float MapLinear(float v, float in_a, float in_b, float out_a, float out_b) {
    if (!std::isfinite(v) || !std::isfinite(in_a) || !std::isfinite(in_b) ||
        !std::isfinite(out_a) || !std::isfinite(out_b)) {
        return SafeFinite(out_a, 0.0f);
    }

    if (std::abs(in_b - in_a) < 1e-6f) {
        return out_a;
    }

    const float v_clamped = ClampRange(v, in_a, in_b);
    const float t = (v_clamped - in_a) / (in_b - in_a);
    return out_a + (out_b - out_a) * t;
}

static std::optional<std::pair<float, float>> ReadVec2(const JsonValue &v) {
    const JsonArray *arr = v.asArray();
    if (!arr || arr->size() < 2) {
        return std::nullopt;
    }
    const double *x = (*arr)[0].asNumber();
    const double *y = (*arr)[1].asNumber();
    if (!x || !y) {
        return std::nullopt;
    }
    return std::make_pair(static_cast<float>(*x), static_cast<float>(*y));
}

static float NormalizeColorChannel(float v) {
    if (!std::isfinite(v)) {
        return 1.0f;
    }
    if (v < 0.0f || v > 1.0f) {
        return Clamp01(v / 255.0f);
    }
    return Clamp01(v);
}

static std::optional<SDL_FColor> ReadColor(const JsonValue &v) {
    const JsonArray *arr = v.asArray();
    if (!arr || arr->size() < 3) {
        return std::nullopt;
    }

    const double *r = (*arr)[0].asNumber();
    const double *g = (*arr)[1].asNumber();
    const double *b = (*arr)[2].asNumber();
    const double *a = (arr->size() >= 4) ? (*arr)[3].asNumber() : nullptr;
    if (!r || !g || !b) {
        return std::nullopt;
    }

    SDL_FColor c{};
    c.r = NormalizeColorChannel(static_cast<float>(*r));
    c.g = NormalizeColorChannel(static_cast<float>(*g));
    c.b = NormalizeColorChannel(static_cast<float>(*b));
    c.a = a ? NormalizeColorChannel(static_cast<float>(*a)) : 1.0f;
    return c;
}

static std::string JoinPath(const std::string &base_dir, const std::string &rel) {
    if (rel.empty()) {
        return rel;
    }
    if (base_dir.empty()) {
        return rel;
    }
    const char last = base_dir.back();
    if (last == '/' || last == '\\') {
        return base_dir + rel;
    }
    return base_dir + "/" + rel;
}

static std::string DirName(const std::string &path) {
    const std::size_t p = path.find_last_of("/\\");
    if (p == std::string::npos) {
        return std::string();
    }
    return path.substr(0, p);
}

static std::optional<BindingType> ParseBindingType(std::string_view s) {
    if (s == "posX") return BindingType::PosX;
    if (s == "posY") return BindingType::PosY;
    if (s == "rotDeg") return BindingType::RotDeg;
    if (s == "scaleX") return BindingType::ScaleX;
    if (s == "scaleY") return BindingType::ScaleY;
    if (s == "opacity") return BindingType::Opacity;
    return std::nullopt;
}

static std::optional<AnimationChannelType> ParseAnimationChannelType(std::string_view s) {
    if (s == "Breath" || s == "breath") return AnimationChannelType::Breath;
    if (s == "Blink" || s == "blink") return AnimationChannelType::Blink;
    if (s == "HeadSway" || s == "headSway" || s == "headsway") return AnimationChannelType::HeadSway;
    return std::nullopt;
}

static std::optional<AnimationBlendMode> ParseAnimationBlendMode(std::string_view s) {
    if (s == "Add" || s == "add") return AnimationBlendMode::Add;
    if (s == "Override" || s == "override") return AnimationBlendMode::Override;
    return std::nullopt;
}

static std::optional<TimelineInterpolation> ParseTimelineInterpolation(std::string_view s) {
    if (s == "Step" || s == "step") return TimelineInterpolation::Step;
    if (s == "Linear" || s == "linear") return TimelineInterpolation::Linear;
    if (s == "Hermite" || s == "hermite") return TimelineInterpolation::Hermite;
    return std::nullopt;
}

static float EvalAnimationChannel(const AnimationChannel &channel, float time_sec) {
    const float phase = channel.phase + time_sec * channel.frequency * 6.283185307179586f;
    float signal = 0.0f;

    switch (channel.type) {
        case AnimationChannelType::Breath:
            signal = std::sin(phase);  // [-1, 1]
            break;
        case AnimationChannelType::HeadSway:
            signal = std::sin(phase);  // [-1, 1]
            break;
        case AnimationChannelType::Blink: {
            const float s = std::max(0.0f, std::sin(phase));
            signal = 1.0f - std::pow(1.0f - s, 6.0f);  // [0, 1], 快闭快开
            break;
        }
    }

    return channel.bias + channel.amplitude * signal;
}

static void AppendError(std::string *out_error, const std::string &msg) {
    if (!out_error) {
        return;
    }
    if (!out_error->empty() && out_error->back() != '\n') {
        out_error->push_back('\n');
    }
    out_error->append(msg);
}

}  // namespace

bool LoadModelRuntime(SDL_Renderer *renderer,
                      const char *model_json_path,
                      ModelRuntime *out_model,
                      std::string *out_error) {
    if (out_error) out_error->clear();
    if (!renderer || !out_model || !model_json_path || model_json_path[0] == '\0') {
        if (out_error) {
            *out_error = "LoadModelRuntime invalid args (renderer/out_model/model_json_path)";
        }
        return false;
    }

    *out_model = ModelRuntime{};
    out_model->model_path = model_json_path;

    SDL_IOStream *io = SDL_IOFromFile(model_json_path, "rb");
    if (!io) {
        if (out_error) {
            *out_error = "SDL_IOFromFile failed for model path '" + out_model->model_path + "': " + SDL_GetError();
        }
        return false;
    }

    const Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) {
        SDL_CloseIO(io);
        if (out_error) {
            *out_error = "model file is empty or unreadable: '" + out_model->model_path + "' (size=" +
                         std::to_string(static_cast<long long>(sz)) + ")";
        }
        return false;
    }

    std::string text;
    text.resize(static_cast<std::size_t>(sz));
    const size_t got = SDL_ReadIO(io, text.data(), text.size());
    SDL_CloseIO(io);
    if (got != text.size()) {
        if (out_error) {
            *out_error = "failed to read model file '" + out_model->model_path + "' (expected=" +
                         std::to_string(text.size()) + ", got=" + std::to_string(got) + ")";
        }
        return false;
    }

    JsonParseError jerr;
    auto root_opt = ParseJson(text, &jerr);
    if (!root_opt) {
        if (out_error) {
            *out_error = "json parse error in '" + out_model->model_path + "' at line " +
                         std::to_string(jerr.line) + ", column " + std::to_string(jerr.column) +
                         " (offset=" + std::to_string(jerr.offset) + "): " + jerr.message;
        }
        return false;
    }

    const JsonValue &root = *root_opt;
    if (!root.isObject()) {
        if (out_error) {
            *out_error = "invalid model root in '" + out_model->model_path + "': root must be object";
        }
        return false;
    }

    out_model->source_root_json = root;

    const std::string model_dir = DirName(out_model->model_path);

    if (const JsonValue *params_v = root.get("parameters"); params_v) {
        if (!params_v->isArray()) {
            if (out_error) {
                *out_error = "invalid field 'parameters' in '" + out_model->model_path + "': must be array";
            }
            return false;
        }

        const JsonArray *params = params_v->asArray();
        for (std::size_t param_i = 0; param_i < params->size(); ++param_i) {
            const JsonValue &pv = (*params)[param_i];
            if (!pv.isObject()) {
                AppendError(out_error,
                            "[warn] skip parameters[" + std::to_string(param_i) + "]: item must be object");
                continue;
            }

            ModelParameter p;
            p.id = pv.getString("id").value_or(std::string());
            if (p.id.empty()) {
                AppendError(out_error,
                            "[warn] skip parameters[" + std::to_string(param_i) + "]: missing/empty field 'id'");
                continue;
            }

            FloatParamSpec spec;
            spec.min_value = static_cast<float>(pv.getNumber("min").value_or(0.0));
            spec.max_value = static_cast<float>(pv.getNumber("max").value_or(1.0));
            spec.default_value = static_cast<float>(pv.getNumber("default").value_or(0.0));
            p.param = FloatParam(spec);
            p.param.Reset();

            out_model->param_index[p.id] = static_cast<int>(out_model->parameters.size());
            out_model->parameters.push_back(std::move(p));
        }
    }

    if (const JsonValue *channels_v = root.get("animationChannels"); channels_v && channels_v->isArray()) {
        const JsonArray *channels = channels_v->asArray();
        out_model->animation_channels.reserve(channels->size());

        for (std::size_t i = 0; i < channels->size(); ++i) {
            const JsonValue &cv = (*channels)[i];
            if (!cv.isObject()) {
                AppendError(out_error,
                            "[warn] skip animationChannels[" + std::to_string(i) + "]: item must be object");
                continue;
            }

            AnimationChannel ch;
            ch.id = cv.getString("id").value_or(std::string("channel_" + std::to_string(i)));
            ch.enabled = cv.getBool("enabled").value_or(true);
            ch.weight = static_cast<float>(cv.getNumber("weight").value_or(1.0));
            ch.frequency = static_cast<float>(cv.getNumber("frequency").value_or(1.0));
            ch.amplitude = static_cast<float>(cv.getNumber("amplitude").value_or(1.0));
            ch.bias = static_cast<float>(cv.getNumber("bias").value_or(0.0));
            ch.phase = static_cast<float>(cv.getNumber("phase").value_or(0.0));

            const std::string type_str = cv.getString("type").value_or(std::string());
            const std::string blend_str = cv.getString("blend").value_or(std::string("Add"));
            const std::string param_id = cv.getString("param").value_or(std::string());

            auto type_opt = ParseAnimationChannelType(type_str);
            auto blend_opt = ParseAnimationBlendMode(blend_str);
            auto p_it = out_model->param_index.find(param_id);

            if (!type_opt.has_value() || p_it == out_model->param_index.end()) {
                AppendError(out_error,
                            "[warn] skip animation channel id='" + ch.id +
                            "': invalid type or param (type='" + type_str + "', param='" + param_id + "')");
                continue;
            }

            ch.type = *type_opt;
            ch.blend = blend_opt.value_or(AnimationBlendMode::Add);
            ch.param_index = p_it->second;
            ch.weight = std::clamp(ch.weight, 0.0f, 1.0f);
            ch.frequency = std::max(0.0f, ch.frequency);

            if (const JsonValue *kfs_v = cv.get("keyframes"); kfs_v && kfs_v->isArray() && kfs_v->asArray()) {
                if (const std::string interp_str = cv.getString("interp").value_or(std::string("Linear")); !interp_str.empty()) {
                    ch.timeline_interp = ParseTimelineInterpolation(interp_str).value_or(TimelineInterpolation::Linear);
                }

                for (const JsonValue &kv : *kfs_v->asArray()) {
                    if (!kv.isObject()) continue;
                    const float t = static_cast<float>(kv.getNumber("time").value_or(0.0));
                    const float v = static_cast<float>(kv.getNumber("value").value_or(0.0));
                    const float tin = static_cast<float>(kv.getNumber("inTangent").value_or(0.0));
                    const float tout = static_cast<float>(kv.getNumber("outTangent").value_or(0.0));
                    ch.keyframes.push_back(TimelineKeyframe{
                        .time_sec = t,
                        .value = v,
                        .in_tangent = tin,
                        .out_tangent = tout,
                    });
                }
                std::sort(ch.keyframes.begin(), ch.keyframes.end(), [](const TimelineKeyframe &a, const TimelineKeyframe &b) {
                    return a.time_sec < b.time_sec;
                });
            }

            out_model->animation_channels.push_back(std::move(ch));
        }
    }

    if (const JsonValue *parts_v = root.get("parts"); parts_v) {
        if (!parts_v->isArray()) {
            if (out_error) {
                *out_error = "invalid field 'parts' in '" + out_model->model_path + "': must be array";
            }
            return false;
        }

        const JsonArray *parts = parts_v->asArray();
        out_model->parts.reserve(parts->size());
        std::vector<std::size_t> source_part_indices;
        source_part_indices.reserve(parts->size());

        for (std::size_t part_i = 0; part_i < parts->size(); ++part_i) {
            const JsonValue &pv = (*parts)[part_i];
            if (!pv.isObject()) {
                AppendError(out_error,
                            "[warn] skip parts[" + std::to_string(part_i) + "]: item must be object");
                continue;
            }

            ModelPart part;
            part.id = pv.getString("id").value_or(std::string());
            if (part.id.empty()) {
                AppendError(out_error,
                            "[warn] skip parts[" + std::to_string(part_i) + "]: missing/empty field 'id'");
                continue;
            }

            part.draw_order = static_cast<int>(pv.getNumber("drawOrder").value_or(0.0));

            if (const JsonValue *size_v = pv.get("size")) {
                if (auto sz2 = ReadVec2(*size_v)) {
                    part.width = std::max(1.0f, sz2->first);
                    part.height = std::max(1.0f, sz2->second);
                }
            }
            if (const JsonValue *pivot_v = pv.get("pivot")) {
                if (auto p2 = ReadVec2(*pivot_v)) {
                    part.pivot_x = p2->first;
                    part.pivot_y = p2->second;
                }
            }

            if (const JsonValue *tf = pv.get("transform"); tf && tf->isObject()) {
                if (const JsonValue *pos = tf->get("pos")) {
                    if (auto p2 = ReadVec2(*pos)) {
                        part.base_pos_x = p2->first;
                        part.base_pos_y = p2->second;
                    }
                }
                part.base_rot_deg = static_cast<float>(tf->getNumber("rotDeg").value_or(0.0));
                if (const JsonValue *sc = tf->get("scale")) {
                    if (auto s2 = ReadVec2(*sc)) {
                        part.base_scale_x = s2->first;
                        part.base_scale_y = s2->second;
                    }
                }
                part.base_opacity = static_cast<float>(tf->getNumber("opacity").value_or(1.0));
            }

            if (const JsonValue *color_v = pv.get("color")) {
                if (auto c = ReadColor(*color_v)) {
                    part.color = *c;
                } else {
                    AppendError(out_error,
                                "[warn] invalid color for parts[" + std::to_string(part_i) + "] id='" +
                                part.id + "' (expect [r,g,b] or [r,g,b,a])");
                }
            }

            if (const JsonValue *deformer_v = pv.get("deformer"); deformer_v && deformer_v->isObject()) {
                const std::string type = deformer_v->getString("type").value_or(std::string("warp"));
                if (type == "rotation" || type == "Rotation") {
                    part.deformer_type = DeformerType::Rotation;
                } else {
                    part.deformer_type = DeformerType::Warp;
                }
                part.rotation_deformer_weight = std::clamp(static_cast<float>(deformer_v->getNumber("weight").value_or(0.0)),
                                                           0.0f,
                                                           1.0f);
                part.rotation_deformer_speed = std::max(0.0f, static_cast<float>(deformer_v->getNumber("speed").value_or(2.0)));
            }

            part.runtime_pos_x = part.base_pos_x;
            part.runtime_pos_y = part.base_pos_y;
            part.runtime_rot_deg = part.base_rot_deg;
            part.runtime_scale_x = part.base_scale_x;
            part.runtime_scale_y = part.base_scale_y;
            part.runtime_opacity = Clamp01(part.base_opacity);

            part.deform.ResetDefaults();
            part.deform.translate_x.SetValueImmediate(part.runtime_pos_x);
            part.deform.translate_y.SetValueImmediate(part.runtime_pos_y);
            part.deform.rotation_deg.SetValueImmediate(part.runtime_rot_deg);
            part.deform.scale_x.SetValueImmediate(part.runtime_scale_x);
            part.deform.scale_y.SetValueImmediate(part.runtime_scale_y);

            part.ffd.Resize(4, 4);
            part.ffd.ClearOffsets();
            part.ffd.weight.SetValueImmediate(part.deformer_type == DeformerType::Warp ? 0.0f : 0.0f);

            GridMeshOptions opts;
            opts.width = part.width;
            opts.height = part.height;
            opts.x_segments = 10;
            opts.y_segments = 10;
            opts.edge_rings = 3;
            opts.edge_bias = 2.0f;
            part.mesh = GenerateEdgeDensifiedGrid(opts);

            // Shift mesh so pivot is local origin.
            for (std::size_t i = 0; i + 1 < part.mesh.positions.size(); i += 2) {
                part.mesh.positions[i] -= part.pivot_x;
                part.mesh.positions[i + 1] -= part.pivot_y;
            }

            part.deformed_positions = part.mesh.positions;

            std::string texture_rel = pv.getString("texture").value_or(std::string());
            if (!texture_rel.empty()) {
                std::string tex_path = JoinPath(model_dir, texture_rel);
                part.texture_cache_key = tex_path;

                auto cache_it = out_model->texture_cache.find(tex_path);
                if (cache_it != out_model->texture_cache.end() && cache_it->second.texture) {
                    cache_it->second.ref_count += 1;
                    part.texture = cache_it->second.texture;
                } else {
                    std::string tex_err;
                    SDL_Texture *loaded = LoadPngTexture(renderer, tex_path.c_str(), nullptr, nullptr, &tex_err);
                    if (!loaded) {
                        AppendError(out_error,
                                    "[warn] failed to load texture for parts[" + std::to_string(part_i) +
                                    "] id='" + part.id + "' path='" + tex_path + "': " + tex_err);
                    } else {
                        TextureCacheEntry entry;
                        entry.texture = loaded;
                        entry.ref_count = 1;
                        out_model->texture_cache[tex_path] = entry;
                        part.texture = loaded;
                    }
                }
            }

            if (const JsonValue *bindings_v = pv.get("bindings"); bindings_v && bindings_v->isArray()) {
                const JsonArray *arr = bindings_v->asArray();
                for (const JsonValue &bv : *arr) {
                    if (!bv.isObject()) continue;
                    const std::string pid = bv.getString("param").value_or(std::string());
                    const std::string type_str = bv.getString("type").value_or(std::string());
                    if (pid.empty() || type_str.empty()) continue;

                    auto p_it = out_model->param_index.find(pid);
                    auto t_opt = ParseBindingType(type_str);
                    if (p_it == out_model->param_index.end() || !t_opt.has_value()) {
                        AppendError(out_error,
                                    "[warn] skip binding in part id='" + part.id + "' (parts[" +
                                    std::to_string(part_i) + "]): invalid param='" + pid +
                                    "' or type='" + type_str + "'");
                        continue;
                    }

                    ParamBinding b;
                    b.param_index = p_it->second;
                    b.type = *t_opt;

                    if (const JsonValue *in_v = bv.get("input")) {
                        if (auto in2 = ReadVec2(*in_v)) {
                            b.in_min = in2->first;
                            b.in_max = in2->second;
                        }
                    }
                    if (const JsonValue *out_v = bv.get("output")) {
                        if (auto out2 = ReadVec2(*out_v)) {
                            b.out_min = out2->first;
                            b.out_max = out2->second;
                        }
                    }

                    part.bindings.push_back(b);
                }
            }

            // Temporarily store parent id in map via negative index resolution pass.
            const std::string parent_id = pv.getString("parent").value_or(std::string());
            part.parent_index = -1;

            out_model->part_index[part.id] = static_cast<int>(out_model->parts.size());
            out_model->parts.push_back(std::move(part));
            source_part_indices.push_back(part_i);

            // keep parent mapping in second pass using root array by same index
            (void)parent_id;
        }

        // Parent resolve pass (按“成功加载的 part -> 源 JSON 索引”映射，避免跳过无效 part 后错位)。
        for (std::size_t loaded_i = 0; loaded_i < out_model->parts.size() && loaded_i < source_part_indices.size(); ++loaded_i) {
            const std::size_t src_i = source_part_indices[loaded_i];
            if (src_i >= parts->size()) continue;

            const JsonValue &pv = (*parts)[src_i];
            const std::string parent_id = pv.getString("parent").value_or(std::string());
            if (parent_id.empty()) continue;

            auto it = out_model->part_index.find(parent_id);
            if (it != out_model->part_index.end()) {
                out_model->parts[loaded_i].parent_index = it->second;
            } else {
                AppendError(out_error,
                            "[warn] unresolved parent id='" + parent_id + "' for part id='" +
                            out_model->parts[loaded_i].id + "'");
            }
        }
    }

    if (out_model->parts.empty()) {
        if (out_error) {
            if (out_error->empty()) {
                *out_error = "model has no valid parts in '" + out_model->model_path + "'";
            } else {
                *out_error += "\n[fatal] model has no valid parts in '" + out_model->model_path + "'";
            }
        }
        return false;
    }

    out_model->draw_order_indices.resize(out_model->parts.size());
    for (std::size_t i = 0; i < out_model->parts.size(); ++i) {
        out_model->draw_order_indices[i] = static_cast<int>(i);
    }
    std::stable_sort(out_model->draw_order_indices.begin(), out_model->draw_order_indices.end(),
                     [&](int a, int b) {
                         return out_model->parts[a].draw_order < out_model->parts[b].draw_order;
                     });

    return true;
}

void DestroyModelRuntime(ModelRuntime *model) {
    if (!model) return;
    for (ModelPart &p : model->parts) {
        if (!p.texture_cache_key.empty()) {
            auto it = model->texture_cache.find(p.texture_cache_key);
            if (it != model->texture_cache.end() && it->second.ref_count > 0) {
                it->second.ref_count -= 1;
            }
        }
        p.texture = nullptr;
    }

    for (auto &kv : model->texture_cache) {
        if (kv.second.texture) {
            SDL_DestroyTexture(kv.second.texture);
            kv.second.texture = nullptr;
        }
    }
    model->texture_cache.clear();
    *model = ModelRuntime{};
}


void UpdateModelRuntime(ModelRuntime *model, float time_sec, float dt_sec) {
    if (!model) return;

    model->test_anim_time = time_sec;

    ApplyAnimationChannelTargets(model, time_sec);
    UpdateParameterValues(model, dt_sec);
    if (!UpdateDirtyCachesAndCheckChanged(model)) {
        return;
    }

    ResolveLocalPartStates(model, time_sec, dt_sec);
    ResolveWorldTransforms(model);
    ApplyWorldDeforms(model, dt_sec);
}

void RenderModelRuntime(SDL_Renderer *renderer,
                        const ModelRuntime *model,
                        float view_pan_x,
                        float view_pan_y,
                        float view_zoom) {
    if (!renderer || !model) return;

    const float zoom = std::clamp(view_zoom, 0.1f, 10.0f);

    model->debug_stats.part_count = static_cast<int>(model->parts.size());
    model->debug_stats.drawn_part_count = 0;
    model->debug_stats.vertex_count = 0;
    model->debug_stats.index_count = 0;
    model->debug_stats.triangle_count = 0;


    SDL_Rect head_clip{0, 0, 0, 0};
    bool head_clip_valid = false;
    if (model->enable_simple_mask) {
        auto head_it = model->part_index.find("HeadBase");
        if (head_it != model->part_index.end()) {
            const int idx = head_it->second;
            if (idx >= 0 && idx < static_cast<int>(model->parts.size())) {
                const ModelPart &head = model->parts[static_cast<std::size_t>(idx)];
                if (head.deformed_positions.size() >= 2) {
                    float min_x = head.deformed_positions[0] * zoom + view_pan_x;
                    float min_y = head.deformed_positions[1] * zoom + view_pan_y;
                    float max_x = min_x;
                    float max_y = min_y;
                    for (std::size_t i = 0; i + 1 < head.deformed_positions.size(); i += 2) {
                        const float x = head.deformed_positions[i] * zoom + view_pan_x;
                        const float y = head.deformed_positions[i + 1] * zoom + view_pan_y;
                        min_x = std::min(min_x, x);
                        min_y = std::min(min_y, y);
                        max_x = std::max(max_x, x);
                        max_y = std::max(max_y, y);
                    }
                    head_clip.x = static_cast<int>(std::floor(min_x));
                    head_clip.y = static_cast<int>(std::floor(min_y));
                    head_clip.w = std::max(1, static_cast<int>(std::ceil(max_x - min_x)));
                    head_clip.h = std::max(1, static_cast<int>(std::ceil(max_y - min_y)));
                    head_clip_valid = true;
                }
            }
        }
    }

    for (int part_idx : model->draw_order_indices) {
        if (part_idx < 0 || part_idx >= static_cast<int>(model->parts.size())) continue;
        const ModelPart &part = model->parts[static_cast<std::size_t>(part_idx)];

        if (part.deformed_positions.size() != part.mesh.positions.size() ||
            part.mesh.uvs.size() != part.mesh.positions.size()) {
            continue;
        }

        const std::size_t vc = part.deformed_positions.size() / 2;
        ModelPart &part_mut = const_cast<ModelPart &>(part);
        const bool viewport_changed = std::abs(part_mut.cached_render_pan_x - view_pan_x) > 1e-6f ||
                                      std::abs(part_mut.cached_render_pan_y - view_pan_y) > 1e-6f ||
                                      std::abs(part_mut.cached_render_zoom - zoom) > 1e-6f;
        if (part_mut.render_cache_dirty || viewport_changed ||
            part_mut.cached_render_vertices.size() != vc ||
            part_mut.cached_render_indices.size() != part.mesh.indices.size()) {
            part_mut.cached_render_vertices.resize(vc);
            for (std::size_t i = 0; i < vc; ++i) {
                const std::size_t k = i * 2;
                part_mut.cached_render_vertices[i].position.x = part.deformed_positions[k] * zoom + view_pan_x;
                part_mut.cached_render_vertices[i].position.y = part.deformed_positions[k + 1] * zoom + view_pan_y;
                part_mut.cached_render_vertices[i].tex_coord.x = part.mesh.uvs[k];
                part_mut.cached_render_vertices[i].tex_coord.y = part.mesh.uvs[k + 1];
                part_mut.cached_render_vertices[i].color.r = part.color.r;
                part_mut.cached_render_vertices[i].color.g = part.color.g;
                part_mut.cached_render_vertices[i].color.b = part.color.b;
                part_mut.cached_render_vertices[i].color.a = part.color.a * part.runtime_opacity;
            }

            part_mut.cached_render_indices.resize(part.mesh.indices.size());
            for (std::size_t i = 0; i < part.mesh.indices.size(); ++i) {
                part_mut.cached_render_indices[i] = static_cast<int>(part.mesh.indices[i]);
            }

            part_mut.cached_render_pan_x = view_pan_x;
            part_mut.cached_render_pan_y = view_pan_y;
            part_mut.cached_render_zoom = zoom;
            part_mut.render_cache_dirty = false;
        }

        bool use_clip = false;
        if (model->enable_simple_mask && head_clip_valid) {
            const bool face_like = part.id.find("Face") != std::string::npos ||
                                   part.id.find("Eye") != std::string::npos ||
                                   part.id.find("Brow") != std::string::npos ||
                                   part.id.find("Mouth") != std::string::npos;
            use_clip = face_like && part.id != "HeadBase";
        }
        if (use_clip) {
            SDL_SetRenderClipRect(renderer, &head_clip);
        } else {
            SDL_SetRenderClipRect(renderer, nullptr);
        }

        SDL_RenderGeometry(renderer,
                           part.texture,
                           part_mut.cached_render_vertices.data(),
                           static_cast<int>(part_mut.cached_render_vertices.size()),
                           part_mut.cached_render_indices.data(),
                           static_cast<int>(part_mut.cached_render_indices.size()));

        model->debug_stats.drawn_part_count += 1;
        model->debug_stats.vertex_count += static_cast<int>(vc);
        model->debug_stats.index_count += static_cast<int>(part_mut.cached_render_indices.size());
        model->debug_stats.triangle_count += static_cast<int>(part_mut.cached_render_indices.size() / 3);
    }

    SDL_SetRenderClipRect(renderer, nullptr);
}

bool SaveModelRuntimeJson(const ModelRuntime &model,
                          const char *output_json_path,
                          std::string *out_error) {
    if (out_error) out_error->clear();

    if (!output_json_path || output_json_path[0] == '\0') {
        if (out_error) {
            *out_error = "SaveModelRuntimeJson invalid output_json_path";
        }
        return false;
    }

    JsonValue root = model.source_root_json;
    if (!root.isObject()) {
        if (out_error) {
            *out_error = "SaveModelRuntimeJson: source root is not object";
        }
        return false;
    }

    JsonValue *parts_v = root.get("parts");
    JsonArray *parts_arr = parts_v ? parts_v->asArray() : nullptr;
    if (!parts_arr) {
        if (out_error) {
            *out_error = "SaveModelRuntimeJson: source json missing 'parts' array";
        }
        return false;
    }

    std::unordered_map<std::string, JsonValue *> part_json_by_id;
    for (JsonValue &pv : *parts_arr) {
        if (!pv.isObject()) continue;
        std::optional<std::string> id = pv.getString("id");
        if (!id.has_value() || id->empty()) continue;
        part_json_by_id[*id] = &pv;
    }

    auto set_or_add_number = [](JsonValue *obj, const char *key, double value) {
        if (!obj || !obj->isObject()) return;
        if (JsonValue *v = obj->get(key)) {
            v->value = value;
        } else if (JsonObject *o = obj->asObject()) {
            o->emplace(std::string(key), JsonValue::makeNumber(value));
        }
    };

    auto set_or_add_vec2 = [&](JsonValue *obj, const char *key, float x, float y) {
        if (!obj || !obj->isObject()) return;
        JsonArray arr;
        arr.push_back(JsonValue::makeNumber(static_cast<double>(x)));
        arr.push_back(JsonValue::makeNumber(static_cast<double>(y)));
        if (JsonValue *v = obj->get(key)) {
            v->value = std::move(arr);
        } else if (JsonObject *o = obj->asObject()) {
            o->emplace(std::string(key), JsonValue::makeArray(std::move(arr)));
        }
    };

    for (const ModelPart &part : model.parts) {
        auto it = part_json_by_id.find(part.id);
        if (it == part_json_by_id.end()) {
            continue;
        }

        JsonValue *part_obj = it->second;
        if (!part_obj || !part_obj->isObject()) {
            continue;
        }

        JsonValue *transform = part_obj->get("transform");
        if (!transform || !transform->isObject()) {
            if (JsonObject *part_map = part_obj->asObject()) {
                part_map->emplace("transform", JsonValue::makeObject(JsonObject{}));
                transform = part_obj->get("transform");
            }
        }
        if (!transform || !transform->isObject()) {
            continue;
        }

        set_or_add_vec2(transform, "pos", part.base_pos_x, part.base_pos_y);
        set_or_add_number(transform, "rotDeg", static_cast<double>(part.base_rot_deg));
        set_or_add_vec2(transform, "scale", part.base_scale_x, part.base_scale_y);
        set_or_add_number(transform, "opacity", static_cast<double>(part.base_opacity));

        set_or_add_vec2(part_obj, "pivot", part.pivot_x, part.pivot_y);
        set_or_add_number(part_obj, "drawOrder", static_cast<double>(part.draw_order));

        JsonValue *deformer = part_obj->get("deformer");
        if (!deformer || !deformer->isObject()) {
            if (JsonObject *part_map = part_obj->asObject()) {
                part_map->emplace("deformer", JsonValue::makeObject(JsonObject{}));
                deformer = part_obj->get("deformer");
            }
        }
        if (deformer && deformer->isObject()) {
            if (JsonObject *dobj = deformer->asObject()) {
                (*dobj)["type"] = JsonValue::makeString(part.deformer_type == DeformerType::Rotation ? "rotation" : "warp");
                (*dobj)["weight"] = JsonValue::makeNumber(static_cast<double>(std::clamp(part.rotation_deformer_weight, 0.0f, 1.0f)));
                (*dobj)["speed"] = JsonValue::makeNumber(static_cast<double>(std::max(0.0f, part.rotation_deformer_speed)));
            }
        }
    }

    const std::string text = StringifyJson(root, 2);

    SDL_IOStream *io = SDL_IOFromFile(output_json_path, "wb");
    if (!io) {
        if (out_error) {
            *out_error = "SDL_IOFromFile failed for write path '" + std::string(output_json_path) + "': " + SDL_GetError();
        }
        return false;
    }

    const size_t written = SDL_WriteIO(io, text.data(), text.size());
    SDL_CloseIO(io);
    if (written != text.size()) {
        if (out_error) {
            *out_error = "failed to write full json to '" + std::string(output_json_path) + "'";
        }
        return false;
    }

    return true;
}

}  // namespace k2d
