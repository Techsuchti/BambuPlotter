#include "ArtworkMesh.hpp"

#include <cmath>

#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r { namespace Plotter {

namespace {

constexpr double MIN_SEGMENT_LENGTH = 1e-6;

size_t count_segments(const PlotPaths &paths)
{
    size_t n = 0;
    for (const PlotPath &p : paths) {
        if (p.empty())
            continue;
        n += p.points.size() - 1 + (p.closed ? 1 : 0);
    }
    return n;
}

// One closed box per stroke segment: 8 vertices, 12 outward-wound triangles.
// Endpoints are extended by half the stroke width along the segment
// direction so consecutive boxes overlap at corners instead of gapping.
void append_segment_box(indexed_triangle_set &its,
                        const Vec2d &a, const Vec2d &b,
                        double half_width, double height)
{
    const Vec2d d   = b - a;
    const double len = d.norm();
    if (len < MIN_SEGMENT_LENGTH)
        return;
    const Vec2d du = d / len;
    const Vec2d n  = Vec2d(-du.y(), du.x()) * half_width;
    const Vec2d a2 = a - du * half_width;
    const Vec2d b2 = b + du * half_width;

    const int base = int(its.vertices.size());
    const Vec2d corners[4] = {a2 - n, b2 - n, b2 + n, a2 + n};
    for (double z : {0., height})
        for (const Vec2d &c : corners)
            its.vertices.emplace_back(float(c.x()), float(c.y()), float(z));

    // bottom (-Z), top (+Z), then the four sides.
    const int t[12][3] = {
        {0, 2, 1}, {0, 3, 2},
        {4, 5, 6}, {4, 6, 7},
        {0, 1, 5}, {0, 5, 4},
        {1, 2, 6}, {1, 6, 5},
        {2, 3, 7}, {2, 7, 6},
        {3, 0, 4}, {3, 4, 7},
    };
    for (const auto &tri : t)
        its.indices.emplace_back(base + tri[0], base + tri[1], base + tri[2]);
}

} // namespace

indexed_triangle_set artwork_mesh(const PlotPaths &paths, const Vec2d &pivot, const ArtworkMeshParams &params)
{
    // Decimate a display copy if the artwork is too dense; the plot paths
    // themselves are never touched.
    PlotPaths display = paths;
    double tolerance  = 0.05;
    while (count_segments(display) > params.max_segments && tolerance <= 12.8) {
        for (PlotPath &p : display)
            simplify_dp(p.points, tolerance);
        tolerance *= 2.;
    }

    const double half_width = std::max(params.stroke_width, 0.05) * 0.5;
    const double height     = std::max(params.stroke_height, 0.05);

    indexed_triangle_set its;
    const size_t segments = count_segments(display);
    its.vertices.reserve(segments * 8);
    its.indices.reserve(segments * 12);

    for (const PlotPath &path : display) {
        if (path.empty())
            continue;
        auto shifted = [&](size_t i) { return Vec2d(path.points[i] - pivot); };
        for (size_t i = 1; i < path.points.size(); ++i)
            append_segment_box(its, shifted(i - 1), shifted(i), half_width, height);
        if (path.closed && path.points.size() > 2)
            append_segment_box(its, shifted(path.points.size() - 1), shifted(0), half_width, height);
    }
    return its;
}

} } // namespace Slic3r::Plotter
