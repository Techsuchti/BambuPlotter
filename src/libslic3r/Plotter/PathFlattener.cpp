#include "PathFlattener.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace Plotter {

namespace {

// Distance of point `p` from the (infinite) line through `a` and `b`.
double point_line_distance(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    const Vec2d ab = b - a;
    const double len = ab.norm();
    if (len < 1e-12)
        return (p - a).norm();
    return std::abs(ab.x() * (a.y() - p.y()) - ab.y() * (a.x() - p.x())) / len;
}

constexpr int MAX_DEPTH = 24;

} // namespace

PathFlattener::PathFlattener(double tolerance)
    : m_tolerance(std::max(tolerance, 1e-4))
{}

void PathFlattener::flatten_cubic(std::vector<Vec2d> &out,
                                  const Vec2d &p0, const Vec2d &p1,
                                  const Vec2d &p2, const Vec2d &p3) const
{
    if (out.empty())
        out.emplace_back(p0);
    this->subdivide(out, p0, p1, p2, p3, 0);
}

void PathFlattener::subdivide(std::vector<Vec2d> &out,
                              const Vec2d &p0, const Vec2d &p1,
                              const Vec2d &p2, const Vec2d &p3, int depth) const
{
    // Flat enough when both control points are within tolerance of the chord.
    const double d1 = point_line_distance(p1, p0, p3);
    const double d2 = point_line_distance(p2, p0, p3);
    if (depth >= MAX_DEPTH || (d1 <= m_tolerance && d2 <= m_tolerance)) {
        out.emplace_back(p3);
        return;
    }

    // de Casteljau split at t = 0.5.
    const Vec2d p01   = 0.5 * (p0 + p1);
    const Vec2d p12   = 0.5 * (p1 + p2);
    const Vec2d p23   = 0.5 * (p2 + p3);
    const Vec2d p012  = 0.5 * (p01 + p12);
    const Vec2d p123  = 0.5 * (p12 + p23);
    const Vec2d p0123 = 0.5 * (p012 + p123);

    this->subdivide(out, p0, p01, p012, p0123, depth + 1);
    this->subdivide(out, p0123, p123, p23, p3, depth + 1);
}

} } // namespace Slic3r::Plotter
