#ifndef slic3r_PlotterJobBuilder_hpp_
#define slic3r_PlotterJobBuilder_hpp_

#include <string>

#include "PlotterGCodeGenerator.hpp"
#include "PlotterPath.hpp"
#include "PlotterToolProfile.hpp"

namespace Slic3r { namespace Plotter {

struct PlotterJob
{
    bool        ok = false;
    std::string error;
    std::string file_name;       // "<job name>.gcode.3mf"
    std::string plate_gcode;     // full HEADER + CONFIG + EXECUTABLE plate G-code
    std::string container;       // complete .gcode.3mf bytes, ready to upload
    int         estimated_seconds = 0;
    double      draw_length   = 0.;
    double      travel_length = 0.;
};

// Turns ordered pen strokes into a complete, uploadable A1 mini job.
//
// The A1 mini firmware only executes a LAN job when the uploaded gcode.3mf
// looks like a genuine sliced project (see resources/plotter/README.md for
// the hardware findings): the plate G-code needs HEADER_BLOCK and
// CONFIG_BLOCK sections and the container needs the full metadata file set.
// This builder wraps PlotterGCodeGenerator's movement-only output in an
// honest header (real time estimate, so the printer's progress bar works),
// splices in the hardware-verified neutralized CONFIG_BLOCK, and swaps the
// result into the verified container template.
//
// The embedded G-code is re-validated with PlotterSafetyValidator as a final
// gate; building fails (rather than warns) on any violation.
class PlotterJobBuilder
{
public:
    // `resources_plotter_dir` must contain plate_config_block.txt and
    // plot_container_template.gcode.3mf (normally
    // resources_dir() + "/plotter").
    static PlotterJob build(const PlotPaths            &paths,
                            const PlotterToolProfile   &profile,
                            const std::string          &job_name,
                            const std::string          &resources_plotter_dir);

    // Exposed for tests: header text for a generated job.
    static std::string make_header(const GCodeGenResult &gen, const PlotterToolProfile &profile,
                                   int estimated_seconds);

    // Estimate job duration from generator metrics (drawing + travel +
    // pen lifts + fixed firmware prep overhead).
    static int estimate_seconds(const GCodeGenResult &gen, const PlotterToolProfile &profile);
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterJobBuilder_hpp_
