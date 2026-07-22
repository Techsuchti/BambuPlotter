#ifndef slic3r_PlotterArtworkImport_hpp_
#define slic3r_PlotterArtworkImport_hpp_

#include <string>

#include "libslic3r/Plotter/PlotterPath.hpp"

namespace Slic3r {

class Model;

namespace Plotter { struct PlotterToolProfile; }

namespace GUI {

class Plater;

// Imports an SVG file as plot artwork: a real plate ModelObject manipulated
// with the native gizmos. Oversized artwork is uniformly scaled down to fit
// the calibrated paper rectangle (2 mm margin) and placed inside it.
// Shows a message dialog and returns false on failure.
bool import_svg_as_artwork(Plater &plater, const std::string &svg_path);

// Doc-space strokes of every artwork volume in the model, mapped through the
// current instance transforms into PAPER space (mm, origin = calibrated
// paper origin) — ready for PathOptimizer / PlotterGCodeGenerator.
// Returns empty and fills *error when the model holds no artwork, a source
// re-import fails, or an instance is rotated off the plate plane.
Plotter::PlotPaths collect_artwork_paper_paths(const Model &model,
                                               const Plotter::PlotterToolProfile &profile,
                                               std::string *error);

// Saves every artwork object with its current placement as a .bplot v2
// project. Fails (*error) when the plate holds no artwork.
bool save_plotter_project(Plater &plater, const std::string &path, std::string *error);

// Replaces the plate's artwork with the contents of a .bplot project
// (snapshot taken; existing artwork objects are removed first). Shows
// message dialogs on failure.
bool open_plotter_project(Plater &plater, const std::string &path);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterArtworkImport_hpp_
