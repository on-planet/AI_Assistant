#pragma once

#include <SDL3/SDL.h>

#include "deform.h"
#include "json.h"
#include "mesh_generator.h"
#include "params.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace k2d {

enum class BindingType {
    PosX,
    PosY,
    RotDeg,
    ScaleX,
    ScaleY,
    Opacity,
};

struct ParamBinding {
    int param_index = -1;
    BindingType type = BindingType::PosX;
    float in_min = -1.0f;
    float in_max = 1.0f;
    float out_min = 0.0f;
    float out_max = 0.0f;
};

enum class AnimationChannelType {
    Breath,
    Blink,
    HeadSway,
};

enum class AnimationBlendMode {
    Add,
    Override,
};

struct ModelParameter {
    std::string id;
    FloatParam param;
};

struct AnimationChannel {
    std::string id;
    AnimationChannelType type = AnimationChannelType::Breath;
    AnimationBlendMode blend = AnimationBlendMode::Add;

    int param_index = -1;
    bool enabled = true;

    float weight = 1.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float bias = 0.0f;
    float phase = 0.0f;
};

struct ModelPart {
    std::string id;
    int parent_index = -1;
    int draw_order = 0;

    float width = 1.0f;
    float height = 1.0f;
    float pivot_x = 0.0f;
    float pivot_y = 0.0f;

    float base_pos_x = 0.0f;
    float base_pos_y = 0.0f;
    float base_rot_deg = 0.0f;
    float base_scale_x = 1.0f;
    float base_scale_y = 1.0f;
    float base_opacity = 1.0f;

    float runtime_pos_x = 0.0f;
    float runtime_pos_y = 0.0f;
    float runtime_rot_deg = 0.0f;
    float runtime_scale_x = 1.0f;
    float runtime_scale_y = 1.0f;
    float runtime_opacity = 1.0f;

    SDL_FColor color{1.0f, 1.0f, 1.0f, 1.0f};

    Mesh2D mesh;
    std::vector<float> deformed_positions;

    AffineAnimParams deform;
    FFDDeformer ffd;

    std::vector<ParamBinding> bindings;

    // FFD 增量缓存：用于局部更新，避免每帧全量重采样。
    std::vector<FFDControlPointOffset> ffd_prev_offsets;
    float ffd_prev_weight = 0.0f;
    std::vector<float> ffd_vertex_dxdy;  // 每顶点 2 个值(dx,dy)

    SDL_Texture *texture = nullptr;
    std::string texture_cache_key;
};

struct TextureCacheEntry {
    SDL_Texture *texture = nullptr;
    int ref_count = 0;
};

struct ModelDebugStats {
    int part_count = 0;
    int drawn_part_count = 0;
    int vertex_count = 0;
    int index_count = 0;
    int triangle_count = 0;
};

struct ModelRuntime {
    std::string model_path;
    std::vector<ModelParameter> parameters;
    std::unordered_map<std::string, int> param_index;

    std::vector<AnimationChannel> animation_channels;

    std::vector<ModelPart> parts;
    std::unordered_map<std::string, int> part_index;

    // 纹理缓存：key 为加载路径，value 为纹理对象与引用计数。
    std::unordered_map<std::string, TextureCacheEntry> texture_cache;

    std::vector<int> draw_order_indices;

    JsonValue source_root_json;

    bool animation_channels_enabled = true;
    float test_anim_time = 0.0f;
    mutable ModelDebugStats debug_stats;

    // 脏标记缓存：参数值与部件基础变换快照。
    // 用于“参数和基础变换都未变化时跳过重算”。
    std::vector<float> cached_param_values;
    std::vector<float> cached_part_base_signature;  // 每个 part 7 个值：pos/rot/scale/pivot/opacity
};

bool LoadModelRuntime(SDL_Renderer *renderer,
                      const char *model_json_path,
                      ModelRuntime *out_model,
                      std::string *out_error);

void DestroyModelRuntime(ModelRuntime *model);

void UpdateModelRuntime(ModelRuntime *model, float time_sec, float dt_sec);

void RenderModelRuntime(SDL_Renderer *renderer, const ModelRuntime *model);

bool SaveModelRuntimeJson(const ModelRuntime &model,
                          const char *output_json_path,
                          std::string *out_error);

}  // namespace k2d
