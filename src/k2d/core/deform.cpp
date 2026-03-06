#include "deform.h"

#include <algorithm>
#include <cmath>

namespace k2d {

namespace {

static float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

static float SampleBilinear(const std::vector<FFDControlPointOffset> &offsets,
                            int cols,
                            int rows,
                            float u,
                            float v,
                            bool sample_x) {
    if (cols < 2 || rows < 2 || offsets.empty()) {
        return 0.0f;
    }

    const float fx = Clamp01(u) * static_cast<float>(cols - 1);
    const float fy = Clamp01(v) * static_cast<float>(rows - 1);

    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, cols - 1);
    const int y1 = std::min(y0 + 1, rows - 1);

    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    auto get = [&](int x, int y) -> float {
        const auto &cp = offsets[static_cast<std::size_t>(y * cols + x)];
        return sample_x ? cp.dx : cp.dy;
    };

    const float p00 = get(x0, y0);
    const float p10 = get(x1, y0);
    const float p01 = get(x0, y1);
    const float p11 = get(x1, y1);

    const float a = p00 + (p10 - p00) * tx;
    const float b = p01 + (p11 - p01) * tx;
    return a + (b - a) * ty;
}

}  // namespace

void AffineAnimParams::ResetDefaults() {
    translate_x = FloatParam(FloatParamSpec{0.0f, -2048.0f, 2048.0f});
    translate_y = FloatParam(FloatParamSpec{0.0f, -2048.0f, 2048.0f});
    rotation_deg = FloatParam(FloatParamSpec{0.0f, -180.0f, 180.0f});
    scale_x = FloatParam(FloatParamSpec{1.0f, 0.1f, 4.0f});
    scale_y = FloatParam(FloatParamSpec{1.0f, 0.1f, 4.0f});

    translate_x.Reset();
    translate_y.Reset();
    rotation_deg.Reset();
    scale_x.Reset();
    scale_y.Reset();
}

void AffineAnimParams::Update(float dt_seconds) {
    translate_x.Update(dt_seconds, interp_speed);
    translate_y.Update(dt_seconds, interp_speed);
    rotation_deg.Update(dt_seconds, interp_speed);
    scale_x.Update(dt_seconds, interp_speed);
    scale_y.Update(dt_seconds, interp_speed);
}

void FFDDeformer::Resize(int cols, int rows) {
    control_cols = std::max(2, cols);
    control_rows = std::max(2, rows);
    offsets.assign(static_cast<std::size_t>(control_cols * control_rows), FFDControlPointOffset{});
}

void FFDDeformer::ClearOffsets() {
    for (auto &cp : offsets) {
        cp.dx = 0.0f;
        cp.dy = 0.0f;
    }
}

void ApplyAffineDeform(const Mesh2D &src,
                       std::vector<float> *out_positions,
                       const AffineAnimParams &params) {
    if (!out_positions) {
        return;
    }

    *out_positions = src.positions;

    if (out_positions->empty()) {
        return;
    }

    float min_x = (*out_positions)[0];
    float max_x = (*out_positions)[0];
    float min_y = (*out_positions)[1];
    float max_y = (*out_positions)[1];

    for (std::size_t i = 0; i + 1 < out_positions->size(); i += 2) {
        min_x = std::min(min_x, (*out_positions)[i]);
        max_x = std::max(max_x, (*out_positions)[i]);
        min_y = std::min(min_y, (*out_positions)[i + 1]);
        max_y = std::max(max_y, (*out_positions)[i + 1]);
    }

    const float cx = (min_x + max_x) * 0.5f;
    const float cy = (min_y + max_y) * 0.5f;

    const float rad = params.rotation_deg.value() * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);

    const float sx = params.scale_x.value();
    const float sy = params.scale_y.value();
    const float tx = params.translate_x.value();
    const float ty = params.translate_y.value();

    for (std::size_t i = 0; i + 1 < out_positions->size(); i += 2) {
        float x = (*out_positions)[i] - cx;
        float y = (*out_positions)[i + 1] - cy;

        x *= sx;
        y *= sy;

        const float rx = x * c - y * s;
        const float ry = x * s + y * c;

        (*out_positions)[i] = rx + cx + tx;
        (*out_positions)[i + 1] = ry + cy + ty;
    }
}

void ApplyFFDDeform(const Mesh2D &src,
                    std::vector<float> *inout_positions,
                    const FFDDeformer &ffd) {
    if (!inout_positions || inout_positions->size() != src.positions.size()) {
        return;
    }

    if (src.uvs.size() != src.positions.size()) {
        return;
    }

    if (ffd.control_cols < 2 || ffd.control_rows < 2) {
        return;
    }

    if (ffd.offsets.size() != static_cast<std::size_t>(ffd.control_cols * ffd.control_rows)) {
        return;
    }

    const float w = ffd.weight.value();
    if (w <= 0.0f) {
        return;
    }

    for (std::size_t i = 0; i + 1 < inout_positions->size(); i += 2) {
        const float u = src.uvs[i];
        const float v = src.uvs[i + 1];

        const float dx = SampleBilinear(ffd.offsets, ffd.control_cols, ffd.control_rows, u, v, true);
        const float dy = SampleBilinear(ffd.offsets, ffd.control_cols, ffd.control_rows, u, v, false);

        (*inout_positions)[i] += dx * w;
        (*inout_positions)[i + 1] += dy * w;
    }
}

void ApplyRotationDeltaDeform(std::vector<float> *inout_positions, float rotation_deg_delta) {
    if (!inout_positions || inout_positions->size() < 2) {
        return;
    }

    float min_x = (*inout_positions)[0];
    float max_x = (*inout_positions)[0];
    float min_y = (*inout_positions)[1];
    float max_y = (*inout_positions)[1];
    for (std::size_t i = 0; i + 1 < inout_positions->size(); i += 2) {
        min_x = std::min(min_x, (*inout_positions)[i]);
        max_x = std::max(max_x, (*inout_positions)[i]);
        min_y = std::min(min_y, (*inout_positions)[i + 1]);
        max_y = std::max(max_y, (*inout_positions)[i + 1]);
    }

    const float cx = (min_x + max_x) * 0.5f;
    const float cy = (min_y + max_y) * 0.5f;
    const float rad = rotation_deg_delta * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);

    for (std::size_t i = 0; i + 1 < inout_positions->size(); i += 2) {
        const float x = (*inout_positions)[i] - cx;
        const float y = (*inout_positions)[i + 1] - cy;
        (*inout_positions)[i] = x * c - y * s + cx;
        (*inout_positions)[i + 1] = x * s + y * c + cy;
    }
}

void ApplyCombinedDeform(const Mesh2D &src,
                         std::vector<float> *out_positions,
                         const AffineAnimParams &affine,
                         const FFDDeformer &ffd,
                         float affine_weight,
                         float ffd_weight) {
    if (!out_positions) {
        return;
    }

    std::vector<float> affine_positions;
    ApplyAffineDeform(src, &affine_positions, affine);

    const float aw = Clamp01(affine_weight);
    out_positions->resize(src.positions.size());
    for (std::size_t i = 0; i < src.positions.size(); ++i) {
        (*out_positions)[i] = src.positions[i] + (affine_positions[i] - src.positions[i]) * aw;
    }

    FFDDeformer tmp = ffd;
    tmp.weight.SetValueImmediate(Clamp01(ffd_weight));
    ApplyFFDDeform(src, out_positions, tmp);
}

}  // namespace k2d
