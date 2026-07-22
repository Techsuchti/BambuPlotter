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
    };

    // Collects every artwork object's strokes (current transforms), orders
    // them, generates + validates the sendable job G-code, and produces the
    // preview variant. Caches the ordered paper-space paths on success.
    GenerateOutput generate(const Model &model);

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
