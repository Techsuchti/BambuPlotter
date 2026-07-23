#ifndef slic3r_PlotterArtworkImport_hpp_
#define slic3r_PlotterArtworkImport_hpp_

#include <string>
#include <vector>

#include "libslic3r/Plotter/PlotterArtwork.hpp"
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

// One artwork instance, snapshotted so a worker thread can process it
// without touching the Model (markup + transform copies only).
struct ArtworkSnapshot
{
    Plotter::ArtworkInfo info;
    Transform3d          world; // instance matrix * volume matrix
    std::string          name;
};

// MAIN THREAD: cheap gather of every artwork instance. Empty + *error when
// the model holds no artwork.
std::vector<ArtworkSnapshot> collect_artwork_snapshots(const Model &model, std::string *error);

// WORKER-SAFE (pure compute, no GUI, no Model): re-import, painter-order
// fill composition, hatching, centerlines, then map through the snapshot
// transforms into PAPER space (mm, origin = calibrated paper origin) —
// ready for PathOptimizer / PlotterGCodeGenerator. This is the expensive
// step (Clipper + medial axis); never call it on the UI thread.
Plotter::PlotPaths paper_paths_from_snapshots(const std::vector<ArtworkSnapshot> &snapshots,
                                              const Plotter::PlotterToolProfile &profile,
                                              std::string *error);

// Convenience for synchronous callers: snapshots + paper paths in one call.
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
