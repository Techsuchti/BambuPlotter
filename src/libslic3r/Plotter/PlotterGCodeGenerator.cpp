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

    const int f_travel = feed_mm_min(profile.travel_speed);
    const int f_draw   = feed_mm_min(profile.draw_speed);
    const int f_lift   = feed_mm_min(profile.lift_speed);

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

    // Establish a safe Z before the first XY motion.
    g << "G1 Z" << fmt(profile.pen_up_z) << " F" << f_lift << "\n";

    bool  pen_down = false;
    Vec2d pos      = machine_paths.front().points.front(); // updated below
    bool  first    = true;

    for (const PlotPath &path : machine_paths) {
        const Vec2d &start = path.points.front();
        const bool join = !first && pen_down && (start - pos).norm() <= JOIN_TOLERANCE;
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
            g << "G1 X" << fmt(pt.x()) << " Y" << fmt(pt.y()) << " F" << f_draw << "\n";
            result.draw_length += (pt - pos).norm();
            pos = pt;
        };
        for (size_t i = 1; i < path.points.size(); ++i)
            draw_to(path.points[i]);
        if (path.closed)
            draw_to(path.points.front());
    }

    // Finish: pen up, wait for motion to complete, then reset heater targets.
    // The A1 firmware preheats the nozzle (~75C) on its own when a job starts
    // and does NOT cool down at FINISH unless the job commands it — turning
    // heaters OFF is the one thermal command a plotter job must contain.
    g << "G1 Z" << fmt(profile.pen_up_z) << " F" << f_lift << "\n"
      << "M400\n"
      << "M104 S0\n"
      << "M140 S0\n"
      << "; end of plot\n";

    result.ok         = true;
    result.gcode      = g.str();
    result.path_count = machine_paths.size();
    return result;
}

} } // namespace Slic3r::Plotter
