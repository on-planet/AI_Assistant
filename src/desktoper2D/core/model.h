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

namespace desktoper2D {

enum class BindingType {
    PosX,
    PosY,
    RotDeg,
    ScaleX,
    ScaleY,
    Opacity,
};

enum class DeformerType {
    Warp,
    Rotation,
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

enum class TimelineInterpolation {
    Step,
    Linear,
    Hermite,
};

enum class TimelineWrapMode {
    Clamp,
    Loop,
    PingPong,
};

struct TimelineKeyframe {
    std::uint64_t stable_id = 0;
    float time_sec = 0.0f;
    float value = 0.0f;
    // dv/dt（每秒）
    float in_tangent = 0.0f;
    float out_tangent = 0.0f;
    // Bezier 权重手柄（[0,1]，越大表示控制柄更长）
    float in_weight = 0.333f;
    float out_weight = 0.333f;
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
    TimelineInterpolation timeline_interp = TimelineInterpolation::Linear;
    TimelineWrapMode timeline_wrap = TimelineWrapMode::Clamp;
    std::vector<TimelineKeyframe> keyframes;
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

    DeformerType deformer_type = DeformerType::Warp;
    float rotation_deformer_weight = 0.0f;      // [0,1]
    float rotation_deformer_deg = 0.0f;         // 旋转增量角度（度）
    float rotation_deformer_speed = 2.0f;       // UI/运行时调节参数

    std::vector<ParamBinding> bindings;

    // part 级脏标记与缓存复用
    bool transform_dirty = true;
    bool deformer_dirty = true;
    bool ffd_delta_dirty = true;
    float last_rotation_deformer_deg = 0.0f;

    // FFD 增量缓存：用于局部更新，避免每帧全量重采样。
    std::vector<FFDControlPointOffset> ffd_prev_offsets;
    float ffd_prev_weight = 0.0f;
    std::vector<float> ffd_vertex_dxdy;  // 每顶点 2 个值(dx,dy)

    // 渲染缓存：复用顶点/索引构建结果
    std::vector<SDL_Vertex> cached_render_vertices;
    std::vector<int> cached_render_indices;
    bool render_cache_dirty = true;
    float cached_render_pan_x = 0.0f;
    float cached_render_pan_y = 0.0f;
    float cached_render_zoom = 1.0f;

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

    // 脏区优化：仅更新发生变化的 part
    std::vector<std::uint8_t> part_dirty_flags;

    // 最小可用弹簧：用于“头部移动 -> 发丝/挂件延迟摆动”。
    bool spring_initialized = false;
    float spring_head_prev_x = 0.0f;
    float spring_head_prev_y = 0.0f;
    std::vector<float> spring_offset_x;
    std::vector<float> spring_offset_y;
    std::vector<float> spring_vel_x;
    std::vector<float> spring_vel_y;

    bool enable_hair_spring = true;
    bool enable_simple_mask = true;
};

bool LoadModelRuntime(SDL_Renderer *renderer,
                      const char *model_json_path,
                      ModelRuntime *out_model,
                      std::string *out_error);

void DestroyModelRuntime(ModelRuntime *model);

void UpdateModelRuntime(ModelRuntime *model, float time_sec, float dt_sec);

void RenderModelRuntime(SDL_Renderer *renderer,
                        const ModelRuntime *model,
                        float view_pan_x = 0.0f,
                        float view_pan_y = 0.0f,
                        float view_zoom = 1.0f);

bool SaveModelRuntimeJson(const ModelRuntime &model,
                          const char *output_json_path,
                          std::string *out_error);

}  // namespace desktoper2D
