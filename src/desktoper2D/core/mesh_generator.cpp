#include "mesh_generator.h"

#include <algorithm>
#include <cmath>

namespace desktoper2D {

namespace {

static int ClampSegments(int v) {
    return std::max(1, v);
}

static float ClampBias(float v) {
    return std::max(1.0f, v);
}

static std::vector<float> BuildRegularAxis(int segments) {
    segments = ClampSegments(segments);
    std::vector<float> axis(static_cast<std::size_t>(segments + 1));
    for (int i = 0; i <= segments; ++i) {
        axis[static_cast<std::size_t>(i)] = static_cast<float>(i) / static_cast<float>(segments);
    }
    return axis;
}

static std::vector<float> BuildEdgeDensifiedAxis(int base_segments, int edge_rings, float edge_bias) {
    base_segments = ClampSegments(base_segments);
    edge_rings = std::max(0, edge_rings);
    edge_bias = ClampBias(edge_bias);

    const int total_segments = base_segments + edge_rings * 2;
    std::vector<float> axis(static_cast<std::size_t>(total_segments + 1));

    // Ring count controls how strongly we blend to the non-linear distribution.
    const float blend = (edge_rings <= 0)
        ? 0.0f
        : std::min(1.0f, static_cast<float>(edge_rings) / static_cast<float>(edge_rings + base_segments));

    for (int i = 0; i <= total_segments; ++i) {
        const float s = static_cast<float>(i) / static_cast<float>(total_segments);

        // Symmetric non-linear mapping with denser samples near 0 and 1.
        float curved = 0.0f;
        if (s <= 0.5f) {
            curved = 0.5f * std::pow(s * 2.0f, edge_bias);
        } else {
            curved = 1.0f - 0.5f * std::pow((1.0f - s) * 2.0f, edge_bias);
        }

        const float t = s * (1.0f - blend) + curved * blend;
        axis[static_cast<std::size_t>(i)] = t;
    }

    axis.front() = 0.0f;
    axis.back() = 1.0f;
    return axis;
}

static Mesh2D BuildGridFromAxes(const std::vector<float> &xs,
                                const std::vector<float> &ys,
                                float width,
                                float height) {
    Mesh2D mesh;

    const int x_vertices = static_cast<int>(xs.size());
    const int y_vertices = static_cast<int>(ys.size());
    const int x_cells = std::max(0, x_vertices - 1);
    const int y_cells = std::max(0, y_vertices - 1);

    mesh.vertex_count = x_vertices * y_vertices;
    mesh.triangle_count = x_cells * y_cells * 2;

    mesh.positions.reserve(static_cast<std::size_t>(mesh.vertex_count) * 2);
    mesh.uvs.reserve(static_cast<std::size_t>(mesh.vertex_count) * 2);
    mesh.indices.reserve(static_cast<std::size_t>(mesh.triangle_count) * 3);

    for (int y = 0; y < y_vertices; ++y) {
        const float v = ys[static_cast<std::size_t>(y)];
        for (int x = 0; x < x_vertices; ++x) {
            const float u = xs[static_cast<std::size_t>(x)];
            mesh.positions.push_back(u * width);
            mesh.positions.push_back(v * height);
            mesh.uvs.push_back(u);
            mesh.uvs.push_back(v);
        }
    }

    for (int y = 0; y < y_cells; ++y) {
        for (int x = 0; x < x_cells; ++x) {
            const std::uint32_t i0 = static_cast<std::uint32_t>(y * x_vertices + x);
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(x_vertices);
            const std::uint32_t i3 = i2 + 1;

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);

            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    return mesh;
}

}  // namespace

Mesh2D GenerateRegularGrid(const GridMeshOptions &options) {
    const auto xs = BuildRegularAxis(options.x_segments);
    const auto ys = BuildRegularAxis(options.y_segments);
    return BuildGridFromAxes(xs, ys, options.width, options.height);
}

Mesh2D GenerateEdgeDensifiedGrid(const GridMeshOptions &options) {
    const auto xs = BuildEdgeDensifiedAxis(options.x_segments, options.edge_rings, options.edge_bias);
    const auto ys = BuildEdgeDensifiedAxis(options.y_segments, options.edge_rings, options.edge_bias);
    return BuildGridFromAxes(xs, ys, options.width, options.height);
}

}  // namespace desktoper2D
