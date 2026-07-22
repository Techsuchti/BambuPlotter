#ifndef slic3r_SvgPlotImporter_hpp_
#define slic3r_SvgPlotImporter_hpp_

#include <string>

#include "PlotterPath.hpp"

namespace Slic3r { namespace Plotter {

struct SvgImportOptions
{
    // Curve flattening tolerance (mm).
    double curve_tolerance = 0.1;
    // Import only shapes that have a stroke paint. When the document contains
    // no stroked shape at all, fall back to importing every visible shape so
    // fill-only exports (common from vector tools) still produce plottable
    // outlines.
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

struct SvgImportResult
{
    bool      ok = false;
    std::string error;
    PlotPaths paths;          // paper space, mm, Y up, origin bottom-left
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
