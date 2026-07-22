#ifndef slic3r_PlotterToolProfile_hpp_
#define slic3r_PlotterToolProfile_hpp_

#include <algorithm>
#include <string>

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r { namespace Plotter {

// Calibrated machine profile for pen plotting on a Bambu Lab A1 mini.
//
// Coordinate conventions:
//  - All values are millimeters in the printer's machine coordinate space
//    (the absolute coordinates commanded over G-code after homing).
//  - `paper_origin` is the commanded machine XY at which the PEN TIP sits on
//    the paper origin (front-left corner of the drawable area). The pen XY
//    offset is therefore already baked into `paper_origin`; `pen_offset` is
//    kept for the calibration wizard and the plate visualization only.
//  - The safe rectangle [min_x..max_x] x [min_y..max_y] bounds the commanded
//    machine XY, i.e. it is the pen-reachable paper area translated into
//    commanded coordinates. Every G0/G1 target must stay inside it.
//  - `pen_down_z` is the absolute floor: no commanded Z may ever be below it.
//
// Homing note: on the A1 mini, Z homing presses the bed against the nozzle.
// A side-mounted pen whose tip sits below the nozzle tip would be crushed.
// The workflow is: home with NO pen attached, then mount the pen; job G-code
// must not home (see `allow_homing_in_job`, default false).
struct PlotterToolProfile
{
    int    version = 1;

    // Set by the calibration wizard once every field below has been captured
    // on the actual machine. Generators/validators refuse invalid profiles.
    bool   calibrated = false;

    // Side-mounted pen tip offset from the nozzle, informational (mm).
    Vec2d  pen_offset{0., 0.};

    // Commanded machine XY placing the pen tip at the paper origin (mm).
    Vec2d  paper_origin{0., 0.};

    // Safe bounds for commanded XY (mm).
    double min_x = 0.;
    double max_x = 0.;
    double min_y = 0.;
    double max_y = 0.;

    // Commanded Z heights (mm). Invariant: pen_down_z <= pen_contact_z <= pen_up_z.
    double pen_up_z      = 0.; // travel height
    double pen_contact_z = 0.; // pen just touches the paper
    double pen_down_z    = 0.; // plotting height, absolute Z floor

    // Speeds (mm/s).
    double travel_speed = 80.;
    double draw_speed   = 30.;
    double lift_speed   = 10.;

    // Mounted pen tip diameter (mm) - drives on-plate stroke width, the
    // preview line width and the hatch spacing; the motion itself is
    // tip-independent.
    double pen_tip_width = 0.5;

    // Filling of solid SVG areas ("plot what the SVG says").
    bool   fill_enabled         = true;
    int    hatch_pattern        = 0;    // 0 = lines, 1 = concentric
    double hatch_spacing_factor = 0.9;  // spacing = pen_tip_width x factor
    double hatch_angle          = 45.;  // degrees, lines pattern only

    double hatch_spacing() const
        { return std::max(pen_tip_width * hatch_spacing_factor, 0.05); }

    // Permit a single leading G28 in generated jobs. Only safe for pen mounts
    // whose tip clears the bed during Z homing; default is the safe choice.
    bool   allow_homing_in_job = false;

    // Validity ------------------------------------------------------------

    // Returns an empty string when the profile is usable for generating and
    // validating jobs; otherwise a human-readable reason.
    std::string invalid_reason() const;
    bool        is_valid() const { return invalid_reason().empty(); }

    // Derived -------------------------------------------------------------

    double plot_width() const  { return max_x - min_x; }
    double plot_height() const { return max_y - min_y; }

    // Safe rectangle of commanded machine XY.
    BoundingBoxf machine_rect() const
        { return BoundingBoxf(Vec2d(min_x, min_y), Vec2d(max_x, max_y)); }

    // End-of-job presentation height: the pen rises here (upward-only, so
    // always safe) before the bed slides to max Y to hand the sheet to the
    // user. Deterministic from the profile so the generator and the
    // validator agree on the allowed ceiling.
    double present_z() const { return std::min(pen_up_z + 60., 120.); }

    // The same rectangle expressed as PEN positions on the bed (for drawing
    // the calibrated plotting area on the build plate).
    BoundingBoxf pen_rect_on_bed() const
        { return BoundingBoxf(Vec2d(min_x, min_y) + pen_offset, Vec2d(max_x, max_y) + pen_offset); }

    // Persistence ----------------------------------------------------------

    // JSON round-trip. `load` returns false and fills `error` on failure.
    std::string serialize_json() const;
    bool        deserialize_json(const std::string &json_text, std::string *error = nullptr);
    bool        save(const std::string &path, std::string *error = nullptr) const;
    bool        load(const std::string &path, std::string *error = nullptr);
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterToolProfile_hpp_
