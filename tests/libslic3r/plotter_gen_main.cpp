// plotter_gen — developer CLI producing validated movement-only plotter
// G-code through the real PlotterGCodeGenerator + PlotterSafetyValidator.
// Used for the cold dry-run hardware tests before the GUI workflow exists.
//
//   plotter_gen <out.gcode> [--squares N] [--dry-run]
//
// --dry-run uses a provisional profile whose Z floor is 10 mm, so every move
// stays far above the bed (no pen, no paper, cold). Without it, the tool
// refuses to run (the real calibrated profile flow lives in the GUI).
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "libslic3r/Plotter/PathOptimizer.hpp"
#include "libslic3r/Plotter/PlotterGCodeGenerator.hpp"
#include "libslic3r/Plotter/PlotterSafetyValidator.hpp"
#include "libslic3r/Plotter/PlotterToolProfile.hpp"

using namespace Slic3r;
using namespace Slic3r::Plotter;

int main(int argc, char **argv)
{
    std::string out_path;
    int         squares = 5;
    bool        dry_run = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dry-run") == 0)
            dry_run = true;
        else if (std::strcmp(argv[i], "--squares") == 0 && i + 1 < argc)
            squares = std::atoi(argv[++i]);
        else if (out_path.empty())
            out_path = argv[i];
    }
    if (out_path.empty() || squares < 1 || squares > 20) {
        std::fprintf(stderr, "usage: plotter_gen <out.gcode> [--squares N] --dry-run\n");
        return 2;
    }
    if (!dry_run) {
        std::fprintf(stderr, "only --dry-run is supported by this developer tool\n");
        return 2;
    }

    // Provisional DRY-RUN profile: Z floor 10 mm above the bed, slow draw
    // moves so pause/resume can be exercised mid-job.
    PlotterToolProfile profile;
    profile.calibrated    = true;
    profile.pen_offset    = Vec2d(0., 0.);
    profile.paper_origin  = Vec2d(50., 50.);
    profile.min_x         = 20.;
    profile.max_x         = 160.;
    profile.min_y         = 20.;
    profile.max_y         = 160.;
    profile.pen_up_z      = 12.;
    profile.pen_contact_z = 10.5;
    profile.pen_down_z    = 10.;
    profile.travel_speed  = 60.;
    profile.draw_speed    = 15.;
    profile.lift_speed    = 10.;

    // Concentric squares around paper (30, 30): sizes 20, 30, ... mm.
    PlotPaths paths;
    const Vec2d center(30., 30.);
    for (int i = 0; i < squares; ++i) {
        const double size = 20. + 10. * i;
        PlotPaths sq = PlotterGCodeGenerator::test_square(size, center - Vec2d(size / 2., size / 2.));
        paths.insert(paths.end(), sq.begin(), sq.end());
    }

    const PlotPaths ordered = PathOptimizer::optimize(paths, Vec2d(0., 0.));
    const GCodeGenResult gen = PlotterGCodeGenerator::generate(ordered, profile);
    if (!gen.ok) {
        std::fprintf(stderr, "generation FAILED: %s\n", gen.error.c_str());
        return 1;
    }

    const ValidationResult validation = PlotterSafetyValidator::validate(gen.gcode, profile);
    std::fprintf(stderr, "%s\n", validation.summary().c_str());
    if (!validation.ok)
        return 1;

    std::ofstream out(out_path, std::ios::trunc);
    out << gen.gcode;
    out.close();
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::fprintf(stderr, "wrote %s: %zu paths, draw %.1f mm, travel %.1f mm\n",
                 out_path.c_str(), gen.path_count, gen.draw_length, gen.travel_length);
    return 0;
}
