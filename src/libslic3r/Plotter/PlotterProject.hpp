#ifndef slic3r_PlotterProject_hpp_
#define slic3r_PlotterProject_hpp_

#include <string>
#include <vector>

#include "PlotterArtwork.hpp"
#include "PlotterPath.hpp"

namespace Slic3r { namespace Plotter {

// Where an artwork instance sits on the build plate (the ModelInstance
// transform restricted to what 2D artwork allows).
struct ArtworkPlacement
{
    Vec2d  offset{0., 0.};  // bed XY of the artwork pivot (mm)
    double rotation_z = 0.; // radians, about Z
    double scale      = 1.; // uniform
};

struct ProjectArtwork
{
    std::string      name;
    ArtworkInfo      info;
    ArtworkPlacement placement;

    // Doc-space strokes mapped onto the bed (pen-tip ink positions):
    //   bed = R(rotation_z) * scale * (p - pivot) + offset
    PlotPaths placed_paths_bed(std::string *error = nullptr) const;
};

// Plot-intent settings that travel with the project: the pen and fill
// choices that shape the drawing. Deliberately EXCLUDES calibration
// (paper origin, bounds, Z heights, pen offset) - that is machine state,
// and opening a foreign project must never overwrite it.
struct ProjectPlotSettings
{
    double pen_tip_width        = 0.5;
    double travel_speed         = 80.;
    double draw_speed           = 30.;
    double lift_speed           = 10.;
    bool   fill_enabled         = true;
    int    hatch_pattern        = 0;
    double hatch_spacing_factor = 0.9;
    double hatch_angle          = 45.;
};

// A saved plotter project (.bplot, version 2): every imported artwork with
// its embedded SVG markup and its placement on the plate, so a session can
// be reopened exactly as arranged. Stored separately from any 3MF project.
//
// The calibration profile is deliberately NOT part of the project — it is
// machine state and lives in data_dir()/plotter/plotter_profile.json.
struct PlotterProject
{
    static constexpr int FORMAT_VERSION = 2;

    int                         version = FORMAT_VERSION;
    std::vector<ProjectArtwork> artworks;
    // Optional (older v2 files lack it): pen/fill plot intent.
    bool                        has_settings = false;
    ProjectPlotSettings         settings;

    std::string serialize_json() const;
    bool        deserialize_json(const std::string &json_text, std::string *error = nullptr);
    bool        save(const std::string &path, std::string *error = nullptr) const;
    bool        load(const std::string &path, std::string *error = nullptr);
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterProject_hpp_
