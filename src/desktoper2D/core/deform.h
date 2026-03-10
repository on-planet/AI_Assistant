#pragma once

#include "mesh_generator.h"
#include "params.h"

#include <vector>

namespace desktoper2D {

struct AffineAnimParams {
    FloatParam translate_x;
    FloatParam translate_y;
    FloatParam rotation_deg;
    FloatParam scale_x;
    FloatParam scale_y;

    float interp_speed = 8.0f;

    void ResetDefaults();
    void Update(float dt_seconds);
};

struct FFDControlPointOffset {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct FFDDeformer {
    // Control lattice size in points (minimum 2x2).
    int control_cols = 4;
    int control_rows = 4;

    // Row-major control offsets, size = control_cols * control_rows.
    std::vector<FFDControlPointOffset> offsets;

    // [0,1] blending weight with affine result.
    FloatParam weight{FloatParamSpec{0.0f, 0.0f, 1.0f}};

    void Resize(int cols, int rows);
    void ClearOffsets();
};

// Apply affine transform (TRS) to source positions.
void ApplyAffineDeform(const Mesh2D &src,
                       std::vector<float> *out_positions,
                       const AffineAnimParams &params);

// Apply FFD on top of positions using mesh UVs as lattice domain.
void ApplyFFDDeform(const Mesh2D &src,
                    std::vector<float> *inout_positions,
                    const FFDDeformer &ffd);

// 对当前顶点执行绕包围盒中心旋转（角度单位：deg）。
void ApplyRotationDeltaDeform(std::vector<float> *inout_positions, float rotation_deg_delta);

// Apply affine + FFD with additive blending by weights.
void ApplyCombinedDeform(const Mesh2D &src,
                         std::vector<float> *out_positions,
                         const AffineAnimParams &affine,
                         const FFDDeformer &ffd,
                         float affine_weight,
                         float ffd_weight);

}  // namespace desktoper2D
