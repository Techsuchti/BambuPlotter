#ifndef slic3r_PlotterProject_hpp_
#define slic3r_PlotterProject_hpp_

#include <string>
#include <vector>

#include "PlotterPath.hpp"
#include "SvgPlotImporter.hpp"

namespace Slic3r { namespace Plotter {

// A saved plotter project: the imported source plus the placement that turns
// it into machine-space strokes. This is stored separately from any 3MF
// project (BambuPlotter settings live under data_dir()/plotter), so a plot
// can be reopened, re-placed and re-sent without re-importing the SVG.
//
// Placement maps paper/document coordinates into the calibrated plotting
// rectangle: strokes are scaled by `scale` and shifted by `offset` (paper mm).
struct PlotterProject
{
    int         version = 1;
    std::string source_svg_path;   // absolute path, informational
    std::string svg_markup;        // embedded copy so the project is portable
    double      doc_width  = 0.;   // source document size (mm)
    double      doc_height = 0.;

    // Placement within the calibrated rectangle (paper-space mm).
    double scale     = 1.0;
    Vec2d  offset    = Vec2d(0., 0.);

    SvgImportOptions import_options;

    // Re-import strokes from the embedded markup with the stored options.
    // Returns paper-space paths BEFORE placement (raw document coordinates).
    PlotPaths import_paths(std::string *error = nullptr) const;

    // Strokes after scale + offset, ready for PathOptimizer / job building.
    PlotPaths placed_paths(std::string *error = nullptr) const;

    std::string serialize_json() const;
    bool        deserialize_json(const std::string &json_text, std::string *error = nullptr);
    bool        save(const std::string &path, std::string *error = nullptr) const;
    bool        load(const std::string &path, std::string *error = nullptr);

    // Build a project by importing an SVG file (embeds its markup).
    static PlotterProject from_svg_file(const std::string &path,
                                        const SvgImportOptions &options,
                                        std::string *error = nullptr);
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterProject_hpp_
