#ifndef slic3r_PlotterController_hpp_
#define slic3r_PlotterController_hpp_

#include <string>

#include "libslic3r/Plotter/PlotterGCodeGenerator.hpp"
#include "libslic3r/Plotter/PlotterPath.hpp"
#include "libslic3r/Plotter/PlotterToolProfile.hpp"

namespace Slic3r {

class Model;

namespace GUI {

// Plotter application state owned by the Plater: the calibrated tool
// profile and the result of the last "Generate plot". The cached paths are
// what "Send plot" uploads, so the printer always runs exactly what the
// Preview showed.
class PlotterController
{
public:
    // Loads (or reloads, after calibration) the profile from
    // PlotterCalibrationController::profile_path(). Missing file leaves an
    // invalid profile; that is a normal pre-calibration state.
    void reload_profile();

    const Plotter::PlotterToolProfile &profile() const { return m_profile; }
    bool profile_valid() const { return m_profile.is_valid(); }

    struct GenerateOutput
    {
        bool        ok = false;
        std::string error;
        std::string preview_gcode;  // synthetic-E variant for the Preview tab
        double      draw_length   = 0.;
        double      travel_length = 0.;
        size_t      path_count    = 0;
        // Optimizer-ordered paper-space paths; adopted on the main thread
        // after a successful run so Send uploads exactly what was previewed.
        Plotter::PlotPaths ordered_paths;
    };

    // Worker-thread half of Generate plot: pure compute over copied inputs
    // (paths must be collected on the MAIN thread - they read the Model).
    struct GenerateJob
    {
        Plotter::PlotPaths          paths;   // paper space, unordered
        Plotter::PlotterToolProfile profile;
    };
    static GenerateOutput run_generate(GenerateJob &&job);

    // Main-thread adoption of a successful worker result.
    void adopt_result(Plotter::PlotPaths &&ordered_paths)
    {
        m_last_paths = std::move(ordered_paths);
        m_has_result = true;
    }

    // True while the last generated result still matches the plate (cleared
    // whenever the model changes).
    bool has_current_result() const { return m_has_result; }
    void invalidate_result() { m_has_result = false; m_last_paths.clear(); }

    // Ordered paper-space paths of the last successful generate.
    const Plotter::PlotPaths &last_paths() const { return m_last_paths; }

    // Path of the currently open .bplot project ("" = unsaved session).
    const std::string &project_path() const { return m_project_path; }
    void set_project_path(const std::string &path) { m_project_path = path; }

private:
    Plotter::PlotterToolProfile m_profile;
    Plotter::PlotPaths          m_last_paths;
    bool                        m_has_result = false;
    std::string                 m_project_path;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterController_hpp_
