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

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterPath_hpp_
