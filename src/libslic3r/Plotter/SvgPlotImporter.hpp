#ifndef slic3r_SvgPlotImporter_hpp_
#define slic3r_SvgPlotImporter_hpp_

#include <string>

#include "PlotterPath.hpp"

namespace Slic3r { namespace Plotter {

struct SvgImportOptions
{
    // Curve flattening tolerance (mm).
    double curve_tolerance = 0.05;
    // Legacy (fills are first-class now: every painted shape imports its
    // outline, filled shapes additionally report a SvgFillRegion). Kept for
    // serialization compatibility; no longer filters anything.
    bool   prefer_stroked = true;
    // Paths shorter than this (mm) are dropped as noise.
    double min_path_length = 0.05;
    // Douglas-Peucker simplification tolerance applied after flattening (mm).
    // Zero disables simplification.
    double simplify_tolerance = 0.02;

    template<class Archive> void serialize(Archive &ar)
    {
        ar(curve_tolerance, prefer_stroked, min_path_length, simplify_tolerance);
    }
};

// A filled area of the document: its closed contours plus the SVG fill rule
// that decides which enclosed contours are holes (letters, eyes, ...).
// Contours are doc space (mm, Y up), always treated as closed.
// Regions are listed in DOCUMENT ORDER (SVG painter's algorithm): a light
// (paper-colored) fill painted over dark ink means "no ink here", so it
// erases instead of adding.
struct SvgFillRegion
{
    PlotPaths contours;
    bool      even_odd = false; // SVG fill-rule: evenodd vs nonzero
    bool      erases   = false; // light fill: subtract from ink laid below
};

struct SvgImportResult
{
    bool      ok = false;
    std::string error;
    // Stroke-painted geometry (doc space, mm, Y up, origin bottom-left).
    // Filled shapes are NOT here - they live in fill_regions, and their pen
    // plan (outlines, hatch, centerlines) comes from plot_fill_regions().
    PlotPaths paths;
    std::vector<SvgFillRegion> fill_regions;
    double    width  = 0.;    // SVG document size (mm)
    double    height = 0.;
    size_t    shapes_total   = 0;
    size_t    shapes_imported = 0;
};

// Imports SVG path geometry as pen strokes. Unlike the emboss/model SVG
// importers, this preserves open paths (they are never closed, filled or
// healed) and flattens curves into polylines via PathFlattener.
//
// The SVG Y axis (down) is flipped into paper space (Y up), so the drawing
// appears on paper exactly as rendered by an SVG viewer.
class SvgPlotImporter
{
public:
    static SvgImportResult import_file(const std::string &path,
                                       const SvgImportOptions &options = {});
    // Parses SVG markup from memory; used by tests and clipboard import.
    static SvgImportResult import_memory(const std::string &svg_text,
                                         const SvgImportOptions &options = {});
};

} } // namespace Slic3r::Plotter

#endif // slic3r_SvgPlotImporter_hpp_
