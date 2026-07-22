#ifndef slic3r_PlotterPath_hpp_
#define slic3r_PlotterPath_hpp_

#include <vector>

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r { namespace Plotter {

// A single pen stroke: an ordered run of points drawn with the pen down.
// Coordinates are millimeters in "paper space": X right, Y up, origin at the
// paper origin captured by the calibration wizard. Unlike the slicing
// pipeline, open paths are first-class citizens here and are never closed or
// filled.
struct PlotPath
{
    std::vector<Vec2d> points;
    bool               closed = false;

    bool   empty() const { return points.size() < 2; }
    double length() const
    {
        double len = 0.;
        for (size_t i = 1; i < points.size(); ++i)
            len += (points[i] - points[i - 1]).norm();
        return len;
    }
    void reverse() { std::reverse(points.begin(), points.end()); }
};

using PlotPaths = std::vector<PlotPath>;

inline BoundingBoxf get_extents(const PlotPaths &paths)
{
    BoundingBoxf bbox;
    for (const PlotPath &p : paths)
        for (const Vec2d &pt : p.points)
            bbox.merge(pt);
    return bbox;
}

// Total pen-up travel of an ordered sequence, starting from `start`.
// A closed path is drawn back to its first point, so the pen exits a closed
// path where it entered it.
inline double pen_up_travel(const PlotPaths &paths, const Vec2d &start)
{
    double travel = 0.;
    Vec2d  pos    = start;
    for (const PlotPath &p : paths) {
        if (p.empty())
            continue;
        travel += (p.points.front() - pos).norm();
        pos = p.closed ? p.points.front() : p.points.back();
    }
    return travel;
}

// Uniformly scale and translate all paths.
inline void transform(PlotPaths &paths, double scale, const Vec2d &offset)
{
    for (PlotPath &p : paths)
        for (Vec2d &pt : p.points)
            pt = pt * scale + offset;
}

// Douglas-Peucker on Vec2d, preserving first/last points.
inline void simplify_dp(std::vector<Vec2d> &pts, double tolerance)
{
    if (tolerance <= 0. || pts.size() < 3)
        return;
    std::vector<bool> keep(pts.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> stack{{0, pts.size() - 1}};
    while (!stack.empty()) {
        auto [first, last] = stack.back();
        stack.pop_back();
        double max_d = 0.;
        size_t idx   = first;
        const Vec2d &a = pts[first];
        const Vec2d &b = pts[last];
        const Vec2d ab = b - a;
        const double ab_len = ab.norm();
        for (size_t i = first + 1; i < last; ++i) {
            const double d = ab_len < 1e-12 ?
                (pts[i] - a).norm() :
                std::abs(ab.x() * (a.y() - pts[i].y()) - ab.y() * (a.x() - pts[i].x())) / ab_len;
            if (d > max_d) {
                max_d = d;
                idx   = i;
            }
        }
        if (max_d > tolerance) {
            keep[idx] = true;
            stack.emplace_back(first, idx);
            stack.emplace_back(idx, last);
        }
    }
    std::vector<Vec2d> out;
    out.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        if (keep[i])
            out.emplace_back(pts[i]);
    pts = std::move(out);
}

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterPath_hpp_
