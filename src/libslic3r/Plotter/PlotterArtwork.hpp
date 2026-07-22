#ifndef slic3r_PlotterArtwork_hpp_
#define slic3r_PlotterArtwork_hpp_

#include <string>

#include "PlotterPath.hpp"
#include "SvgPlotImporter.hpp"

namespace Slic3r { namespace Plotter {

// Source data of one imported SVG artwork, carried by the ModelVolume that
// displays it on the plate. The volume's mesh is display-only: G-code is
// always regenerated from these paths, transformed by the owning instance.
//
// `pivot` is the doc-space point (usually the ink bounding-box center) that
// was translated to the mesh origin when the display mesh was built. The
// volume transform stays identity, so a doc-space point p maps to the bed as
//   bed = instance_matrix * (p - pivot, 0)
struct ArtworkInfo
{
    std::string source_svg_path;  // absolute path, informational
    std::string svg_markup;       // embedded copy so projects stay portable
    double      doc_width  = 0.;  // source document size (mm)
    double      doc_height = 0.;
    Vec2d       pivot{0., 0.};    // doc-space point baked to the mesh origin
    SvgImportOptions options;

    // Re-import strokes from the embedded markup with the stored options.
    // Returns doc-space paths (mm, Y up), NOT pivot-shifted.
    PlotPaths import_paths(std::string *error = nullptr) const
    {
        const SvgImportResult r = SvgPlotImporter::import_memory(svg_markup, options);
        if (!r.ok) {
            if (error != nullptr)
                *error = r.error;
            return {};
        }
        return r.paths;
    }

    template<class Archive> void serialize(Archive &ar)
    {
        ar(source_svg_path, svg_markup, doc_width, doc_height, pivot(0), pivot(1), options);
    }
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterArtwork_hpp_
