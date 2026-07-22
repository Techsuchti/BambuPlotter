#include "PlotterGCodeGenerator.hpp"

#include <cmath>
#include <sstream>

#include "libslic3r/LocalesUtils.hpp"

namespace Slic3r { namespace Plotter {

namespace {

constexpr double BOUNDS_EPSILON = 1e-6;

std::string fmt(double v)
{
    std::string s = float_to_string_decimal_point(v, 3);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0')
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
    }
    return s;
}

int feed_mm_min(double speed_mm_s) { return int(std::lround(speed_mm_s * 60.)); }

// Shared motion emitter for the sendable job and the preview variant, so the
// two can never diverge in what the pen actually does. `paths` are already in
// output coordinates. When `synthetic_e > 0`, draw moves carry relative E
// (preview only).
void emit_plot_moves(std::ostringstream &g,
                     const PlotPaths &paths,
                     const PlotterToolProfile &profile,
                     double synthetic_e_per_mm,
                     GCodeGenResult &result)
{
    const int f_travel = feed_mm_min(profile.travel_speed);
    const int f_draw   = feed_mm_min(profile.draw_speed);
    const int f_lift   = feed_mm_min(profile.lift_speed);

    // Establish a safe Z before the first XY motion.
    g << "G1 Z" << fmt(profile.pen_up_z) << " F" << f_lift << "\n";

    bool  pen_down = false;
    Vec2d pos      = paths.front().points.front(); // updated below
    bool  first    = true;

    for (const PlotPath &path : paths) {
        const Vec2d &start = path.points.front();
        const bool join = !first && pen_down &&
                          (start - pos).norm() <= PlotterGCodeGenerator::JOIN_TOLERANCE;
        if (!join) {
            if (pen_down) {
                g << "G1 Z" << fmt(profile.pen_up_z) << " F" << f_lift << "\n";
                pen_down = false;
            }
            g << "G1 X" << fmt(start.x()) << " Y" << fmt(start.y()) << " F" << f_travel << "\n";
            if (!first)
                result.travel_length += (start - pos).norm();
            g << "G1 Z" << fmt(profile.pen_down_z) << " F" << f_lift << "\n";
            pen_down = true;
        }
        pos   = start;
        first = false;

        auto draw_to = [&](const Vec2d &pt) {
            const double len = (pt - pos).norm();
            g << "G1 X" << fmt(pt.x()) << " Y" << fmt(pt.y());
            if (synthetic_e_per_mm > 0.)
                g << " E" << fmt(std::max(len * synthetic_e_per_mm, 1e-5));
            g << " F" << f_draw << "\n";
            result.draw_length += len;
            pos = pt;
        };
        for (size_t i = 1; i < path.points.size(); ++i)
            draw_to(path.points[i]);
        if (path.closed)
            draw_to(path.points.front());
    }

    g << "G1 Z" << fmt(profile.pen_up_z) << " F" << f_lift << "\n";
}

} // namespace

PlotPaths PlotterGCodeGenerator::test_square(double size, const Vec2d &corner)
{
    PlotPath square;
    square.closed = true;
    square.points = {
        corner,
        corner + Vec2d(size, 0.),
        corner + Vec2d(size, size),
        corner + Vec2d(0., size),
    };
    return PlotPaths{square};
}

GCodeGenResult PlotterGCodeGenerator::generate(const PlotPaths &paths, const PlotterToolProfile &profile)
{
    GCodeGenResult result;

    if (const std::string reason = profile.invalid_reason(); !reason.empty()) {
        result.error = "plotter profile is not usable: " + reason;
        return result;
    }

    // Transform to machine coordinates and verify bounds up front so no
    // G-code is produced at all for an out-of-bounds job.
    PlotPaths machine_paths;
    machine_paths.reserve(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        const PlotPath &path = paths[i];
        if (path.empty())
            continue;
        PlotPath mp;
        mp.closed = path.closed;
        mp.points.reserve(path.points.size());
        for (const Vec2d &pt : path.points) {
            const Vec2d m = profile.paper_origin + pt;
            if (m.x() < profile.min_x - BOUNDS_EPSILON || m.x() > profile.max_x + BOUNDS_EPSILON ||
                m.y() < profile.min_y - BOUNDS_EPSILON || m.y() > profile.max_y + BOUNDS_EPSILON) {
                std::ostringstream os;
                os << "path " << i + 1 << " leaves the calibrated plotting area at paper ("
                   << fmt(pt.x()) << ", " << fmt(pt.y()) << ") mm = machine ("
                   << fmt(m.x()) << ", " << fmt(m.y()) << ") mm";
                result.error = os.str();
                return result;
            }
            mp.points.emplace_back(m);
        }
        machine_paths.emplace_back(std::move(mp));
    }

    if (machine_paths.empty()) {
        result.error = "nothing to plot";
        return result;
    }

    std::ostringstream g;
    g << "; BambuPlotter movement-only pen plot\n"
      << "; paths: " << machine_paths.size() << "\n"
      << "; safe rect (machine): X" << fmt(profile.min_x) << ".." << fmt(profile.max_x)
      << " Y" << fmt(profile.min_y) << ".." << fmt(profile.max_y) << "\n"
      << "; pen Z: up " << fmt(profile.pen_up_z) << " / down " << fmt(profile.pen_down_z) << "\n"
      << "; PRECONDITION: axes homed before the pen was mounted; do not re-home with pen attached\n";
    if (profile.allow_homing_in_job)
        g << "G28\n";
    g << "M400\n"
      << "G90\n";

    emit_plot_moves(g, machine_paths, profile, 0., result);

    // Present the finished sheet: pen high first (upward-only, always safe),
    // then the bed slides to max Y — on the A1 mini that pushes the paper
    // toward the user for easy removal. Both targets stay inside the
    // calibrated limits the validator enforces.
    g << "G1 Z" << fmt(profile.present_z()) << " F" << feed_mm_min(profile.travel_speed) << "\n"
      << "G1 Y" << fmt(profile.max_y) << " F" << feed_mm_min(profile.travel_speed) << "\n";

    // Finish: wait for motion to complete, then reset heater targets.
    // The A1 firmware preheats the nozzle (~75C) on its own when a job starts
    // and does NOT cool down at FINISH unless the job commands it — turning
    // heaters OFF is the one thermal command a plotter job must contain.
    g << "M400\n"
      << "M104 S0\n"
      << "M140 S0\n"
      << "; end of plot\n";

    result.ok         = true;
    result.gcode      = g.str();
    result.path_count = machine_paths.size();
    return result;
}

GCodeGenResult PlotterGCodeGenerator::generate_preview(const PlotPaths &paths,
                                                       const PlotterToolProfile &profile,
                                                       double pen_width_mm)
{
    GCodeGenResult result;

    if (const std::string reason = profile.invalid_reason(); !reason.empty()) {
        result.error = "plotter profile is not usable: " + reason;
        return result;
    }

    // Pen-tip bed coordinates: where the ink actually lands, matching the
    // artwork objects on the plate. No bounds check here — the sendable
    // generate() runs first on the same paths and is the gatekeeper.
    PlotPaths bed_paths;
    bed_paths.reserve(paths.size());
    const Vec2d anchor = profile.paper_origin + profile.pen_offset;
    for (const PlotPath &path : paths) {
        if (path.empty())
            continue;
        PlotPath bp;
        bp.closed = path.closed;
        bp.points.reserve(path.points.size());
        for (const Vec2d &pt : path.points)
            bp.points.emplace_back(anchor + pt);
        bed_paths.emplace_back(std::move(bp));
    }

    if (bed_paths.empty()) {
        result.error = "nothing to plot";
        return result;
    }

    std::ostringstream g;
    // First line must contain "BambuStudio" so GCodeProcessor::detect_producer
    // enables the LINE_WIDTH / LAYER_HEIGHT / FEATURE tag parsing; the empty
    // CONFIG_BLOCK is required by Config::load_from_gcode_file.
    g << "; BambuStudio preview variant - generated by BambuPlotter, PREVIEW ONLY, never send\n"
      << "; CONFIG_BLOCK_START\n"
      << "; filament_diameter = 1.75\n"
      << "; CONFIG_BLOCK_END\n"
      << "G90\n"
      << "M83\n"
      << "; LAYER_HEIGHT: 0.2\n"
      << "; LINE_WIDTH: " << fmt(std::max(pen_width_mm, 0.1)) << "\n"
      << "; FEATURE: Outer wall\n";

    // Flatten the preview onto the paper plane: real pen heights (~15mm on
    // this machine) render every lift as a vertical wall and turn the
    // preview into a skyline. Ink at 0.2, lifts at 0.4 - looks like paper.
    // XY motion and speeds are untouched; only the sendable job (generate())
    // carries the real calibrated Z values.
    PlotterToolProfile display = profile;
    display.pen_down_z    = 0.2;
    display.pen_contact_z = 0.3;
    display.pen_up_z      = 0.4;

    // ~0.2x0.4mm line worth of E per drawn mm; the value only needs to be
    // positive, width/height come from the tags above.
    emit_plot_moves(g, bed_paths, display, 0.04, result);

    // Mirror the sendable job's bed-presentation slide (staying flat).
    g << "G1 Y" << fmt(profile.max_y + profile.pen_offset.y()) << " F" << feed_mm_min(profile.travel_speed) << "\n";

    g << "; end of preview\n";

    result.ok         = true;
    result.gcode      = g.str();
    result.path_count = bed_paths.size();
    return result;
}

} } // namespace Slic3r::Plotter
