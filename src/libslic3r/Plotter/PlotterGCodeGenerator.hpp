#ifndef slic3r_PlotterGCodeGenerator_hpp_
#define slic3r_PlotterGCodeGenerator_hpp_

#include <string>

#include "PlotterPath.hpp"
#include "PlotterToolProfile.hpp"

namespace Slic3r { namespace Plotter {

struct GCodeGenResult
{
    bool        ok = false;
    std::string error;
    std::string gcode;
    double      draw_length   = 0.; // mm of pen-down drawing
    double      travel_length = 0.; // mm of pen-up XY travel
    size_t      path_count    = 0;
};

// Generates movement-only pen-plotter G-code. The output contains exclusively
// G90 / G0 / G1 (X/Y/Z/F only) / G4 / M400 and comments — no extrusion, no
// heating, no filament or AMS handling, no tool changes, and by default no
// homing (see PlotterToolProfile::allow_homing_in_job).
//
// This generator never reuses any part of the stock A1 mini print start
// G-code. Every emitted XY coordinate is checked against the calibrated safe
// rectangle and every Z against the calibrated pen heights; a violation
// aborts generation with an error instead of clamping.
class PlotterGCodeGenerator
{
public:
    // `paths` are in paper space (mm, Y up, origin = calibrated paper origin),
    // ideally already ordered by PathOptimizer.
    static GCodeGenResult generate(const PlotPaths &paths, const PlotterToolProfile &profile);

    // PREVIEW-ONLY variant, NEVER sendable: identical motion to generate(),
    // but pen-down strokes carry synthetic relative extrusion (M83 + E) and
    // BambuStudio parser tags ("; FEATURE:", "; LINE_WIDTH:",
    // "; LAYER_HEIGHT:") so the G-code preview renders them like a
    // one-layer print; pen-up moves stay travels. XY is emitted in PEN-TIP
    // bed coordinates (machine XY + profile.pen_offset) so the rendered ink
    // lands exactly on the artwork objects. The safety validator rejects
    // this output by design (E axis); call only after generate() succeeded
    // on the same paths.
    static GCodeGenResult generate_preview(const PlotPaths &paths,
                                           const PlotterToolProfile &profile,
                                           double pen_width_mm = 0.5);

    // The hard-coded 20 mm calibration square used for the first cold
    // hardware test (implementation-order step 12): a single closed square
    // with its lower-left corner at `corner` (paper space).
    static PlotPaths test_square(double size = 20., const Vec2d &corner = Vec2d(0., 0.));

    // When the end point of a stroke and the start of the next are closer
    // than this (mm), the pen is kept down instead of lifting.
    static constexpr double JOIN_TOLERANCE = 0.05;
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterGCodeGenerator_hpp_
