#pragma once

#include <cstdint>
#include <vector>

namespace desktoper2D {

struct Mesh2D {
    // Interleaved as separate streams for easier integration with existing pipelines.
    // positions: [x0, y0, x1, y1, ...]
    std::vector<float> positions;
    // uvs: [u0, v0, u1, v1, ...]
    std::vector<float> uvs;
    // Triangle list indices.
    std::vector<std::uint32_t> indices;

    int vertex_count = 0;
    int triangle_count = 0;
};

struct GridMeshOptions {
    float width = 1.0f;
    float height = 1.0f;

    // Base regular grid segments (minimum 1).
    int x_segments = 8;
    int y_segments = 8;

    // Edge densification rings per side (0 = disabled).
    int edge_rings = 2;

    // >1 increases sampling density near edges. Typical range [1.0, 4.0].
    float edge_bias = 2.0f;
};

// Generate plain regular grid.
Mesh2D GenerateRegularGrid(const GridMeshOptions &options);

// Generate regular grid with edge densification on X/Y axes.
Mesh2D GenerateEdgeDensifiedGrid(const GridMeshOptions &options);

}  // namespace desktoper2D
