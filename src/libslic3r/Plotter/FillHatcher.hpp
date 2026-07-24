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
    // plot_fill_regions only: pen tip diameter (mm) and whether ink parts
    // thinner than ~1.2x the pen become single centerline strokes instead
    // of two overlapping outline passes.
    double       pen_width      = 0.5;
    bool         centerline_thin = true;
    // plot_fill_regions only: adapt stroke density to the pen. Fine artwork
    // hatching packed tighter than the tip would fuse into solid black and
    // deposit multiples of the artwork's ink; like a human inker with a fat
    // pen, draw FEWER strokes instead of fatter ones. Centerlines are kept
    // longest-first; one is dropped when most of its length would re-ink
    // paper this plot has already blackened.
    bool         density_limit  = true;
};

// Interior hatch strokes only (no outlines) for the composed ink of the
// given regions. Painter's-order composition, holes honored per fill rule.
// Output paths are doc space (mm, Y up), unordered (PathOptimizer's job).
PlotPaths hatch_fill_regions(const std::vector<SvgFillRegion> &regions,
                             const HatchParams &params = {});

// The COMPLETE pen plan for the filled areas of a document:
//  - boundary outlines of the composed ink (thick parts),
//  - interior hatch of the thick parts (inset by params.inset),
//  - single medial-axis centerline strokes for parts thinner than
//    ~1.2 x pen width (a pen cannot outline-and-fill those; two outline
//    passes would just draw one fat, stiff line).
PlotPaths plot_fill_regions(const std::vector<SvgFillRegion> &regions,
                            const HatchParams &params = {});

} } // namespace Slic3r::Plotter

#endif // slic3r_FillHatcher_hpp_
