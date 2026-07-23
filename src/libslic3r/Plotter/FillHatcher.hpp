#ifndef slic3r_FillHatcher_hpp_
#define slic3r_FillHatcher_hpp_

#include "PlotterPath.hpp"
#include "SvgPlotImporter.hpp"

namespace Slic3r { namespace Plotter {

enum class HatchPattern : int {
    Lines      = 0, // parallel lines at hatch angle
    Concentric = 1, // inward offsets of the region boundary
};

struct HatchParams
{
    HatchPattern pattern    = HatchPattern::Lines;
    double       spacing    = 0.45; // mm between strokes (pen tip x overlap factor)
    double       angle_deg  = 45.;  // Lines pattern only
    double       min_length = 0.05; // drop shorter hatch fragments (mm)
    // Shrink the hatched area by this much (mm) so strokes - which are pen
    // lines with real width - never bleed past the region boundary and eat
    // thin white details. Set to half the pen tip width.
    double       inset      = 0.;
};

// Turns the filled areas of an imported SVG into interior pen strokes.
// Regions are merged first (overlapping shapes are hatched once), holes are
// honored per the SVG fill rule. The regions' outline contours are NOT
// emitted here - the importer already put them in SvgImportResult::paths.
// Output paths are doc space (mm, Y up), unordered (PathOptimizer's job).
PlotPaths hatch_fill_regions(const std::vector<SvgFillRegion> &regions,
                             const HatchParams &params = {});

} } // namespace Slic3r::Plotter

#endif // slic3r_FillHatcher_hpp_
