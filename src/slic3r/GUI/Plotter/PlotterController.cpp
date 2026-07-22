#include "PlotterController.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Plotter/PathOptimizer.hpp"

#include "slic3r/GUI/Plotter/PlotterArtworkImport.hpp"
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

PlotterController::GenerateOutput PlotterController::generate(const Model &model)
{
    GenerateOutput out;
    invalidate_result();

    if (!m_profile.is_valid()) {
        out.error = "the plotter is not calibrated yet - run the calibration first (" +
                    m_profile.invalid_reason() + ")";
        return out;
    }

    std::string collect_error;
    PlotPaths paths = collect_artwork_paper_paths(model, m_profile, &collect_error);
    if (paths.empty()) {
        out.error = collect_error.empty() ? std::string("nothing to plot") : collect_error;
        return out;
    }

    paths = PathOptimizer::optimize(std::move(paths), Vec2d(0., 0.));

    // The sendable job is the gatekeeper: it bounds-checks every coordinate
    // and refuses out-of-area artwork before any preview is shown.
    const GCodeGenResult send = PlotterGCodeGenerator::generate(paths, m_profile);
    if (!send.ok) {
        out.error = send.error;
        return out;
    }

    const GCodeGenResult preview = PlotterGCodeGenerator::generate_preview(paths, m_profile, m_profile.pen_tip_width);
    if (!preview.ok) {
        out.error = preview.error;
        return out;
    }

    m_last_paths = std::move(paths);
    m_has_result = true;

    out.ok            = true;
    out.preview_gcode = preview.gcode;
    out.draw_length   = send.draw_length;
    out.travel_length = send.travel_length;
    out.path_count    = send.path_count;
    return out;
}

} } // namespace Slic3r::GUI
