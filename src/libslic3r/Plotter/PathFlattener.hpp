#ifndef slic3r_PathFlattener_hpp_
#define slic3r_PathFlattener_hpp_

#include <vector>

#include "libslic3r/Point.hpp"

namespace Slic3r { namespace Plotter {

// Flattens cubic Bezier curves into polylines by adaptive de Casteljau
// subdivision. Pure geometry; used by SvgPlotImporter but independent of any
// SVG library so it can be unit-tested in isolation.
class PathFlattener
{
public:
    // Maximum allowed deviation (mm) between the curve and its polyline
    // approximation.
    explicit PathFlattener(double tolerance = 0.1);

    // Appends the flattened curve to `out`. `p0` is assumed to be already
    // present in `out` (or is pushed if `out` is empty); the remaining points
    // up to and including `p3` are appended.
    void flatten_cubic(std::vector<Vec2d> &out,
                       const Vec2d &p0, const Vec2d &p1,
                       const Vec2d &p2, const Vec2d &p3) const;

    double tolerance() const { return m_tolerance; }

private:
    void subdivide(std::vector<Vec2d> &out,
                   const Vec2d &p0, const Vec2d &p1,
                   const Vec2d &p2, const Vec2d &p3, int depth) const;

    double m_tolerance;
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PathFlattener_hpp_
