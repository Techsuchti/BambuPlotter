#include "PlotterController.hpp"

#include "libslic3r/Plotter/PathOptimizer.hpp"

#include "slic3r/GUI/Plotter/PlotterCalibrationController.hpp"

namespace Slic3r { namespace GUI {

using namespace Slic3r::Plotter;

void PlotterController::reload_profile()
{
    PlotterToolProfile fresh;
    fresh.load(PlotterCalibrationController::profile_path(), nullptr);
    m_profile = fresh;
    // A different calibration invalidates any previously generated plot.
    invalidate_result();
}

PlotterController::GenerateOutput PlotterController::run_generate(GenerateJob &&job)
{
    GenerateOutput out;

    PlotPaths paths = PathOptimizer::optimize(std::move(job.paths), Vec2d(0., 0.));

    // The sendable job is the gatekeeper: it bounds-checks every coordinate
    // and refuses out-of-area artwork before any preview is shown.
    const GCodeGenResult send = PlotterGCodeGenerator::generate(paths, job.profile);
    if (!send.ok) {
        out.error = send.error;
        return out;
    }

    const GCodeGenResult preview = PlotterGCodeGenerator::generate_preview(paths, job.profile, job.profile.pen_tip_width);
    if (!preview.ok) {
        out.error = preview.error;
        return out;
    }

    out.ok            = true;
    out.preview_gcode = preview.gcode;
    out.draw_length   = send.draw_length;
    out.travel_length = send.travel_length;
    out.path_count    = send.path_count;
    out.ordered_paths = std::move(paths);
    return out;
}

} } // namespace Slic3r::GUI
