#ifndef slic3r_PathOptimizer_hpp_
#define slic3r_PathOptimizer_hpp_

#include "PlotterPath.hpp"

namespace Slic3r { namespace Plotter {

// Orders pen strokes to reduce pen-up travel using a greedy nearest-endpoint
// chain. Open paths may be reversed when that shortens the travel; closed
// paths keep their topology and are rotated to start at the vertex nearest
// the pen. O(n * total_points) — fine for plotting workloads.
class PathOptimizer
{
public:
    // `start` is the pen position (paper space, mm) before the first stroke,
    // typically the paper origin (0, 0).
    static PlotPaths optimize(PlotPaths paths, const Vec2d &start = Vec2d(0., 0.));
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PathOptimizer_hpp_
