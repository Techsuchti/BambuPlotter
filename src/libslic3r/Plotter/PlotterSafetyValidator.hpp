#ifndef slic3r_PlotterSafetyValidator_hpp_
#define slic3r_PlotterSafetyValidator_hpp_

#include <string>
#include <vector>

#include "PlotterToolProfile.hpp"

namespace Slic3r { namespace Plotter {

struct ValidationIssue
{
    size_t      line_number = 0; // 1-based
    std::string line;            // offending line (trimmed)
    std::string reason;
};

struct ValidationResult
{
    bool                         ok = false;
    std::vector<ValidationIssue> issues;

    std::string summary() const;
};

// Final, strict gate every plotter job must pass before upload. The validator
// is ALLOWLIST based: anything not explicitly permitted is rejected, so every
// prohibited operation (extrusion/E axis, nozzle/bed heating, filament and
// AMS handling, purging/wiping/priming, tool changes, out-of-limits motion,
// Z below the calibrated pen-down floor) fails validation — it rejects
// instead of warning.
//
// Permitted grammar:
//   - blank lines and `;` comments
//   - G90 (absolute positioning; required before any motion)
//   - G0/G1 with X/Y/Z/F parameters only, XY inside the calibrated safe
//     rectangle, Z within [pen_down_z, pen_up_z], F within (0, 300 mm/s];
//     the first motion must set F and no XY motion may precede the first Z
//   - G4 with S/P dwell parameters
//   - M400 (wait for moves to finish)
//   - M104 S0 and M140 S0 exactly (heaters OFF; the firmware preheats on its
//     own at job start and only cools down when the job commands it — any
//     non-zero heater target is rejected)
//   - a single bare G28 before any motion, and only when the profile opts in
//     via allow_homing_in_job (homing with a mounted pen can crush the pen)
// File-level rules: at least one motion command, and the final commanded Z
// must equal pen_up_z (the job must end with the pen raised).
class PlotterSafetyValidator
{
public:
    static ValidationResult validate(const std::string &gcode, const PlotterToolProfile &profile);

    static constexpr double BOUNDS_EPSILON = 1e-3; // mm
    static constexpr double MAX_FEED       = 300. * 60.; // mm/min
    static constexpr size_t MAX_ISSUES     = 50;
};

} } // namespace Slic3r::Plotter

#endif // slic3r_PlotterSafetyValidator_hpp_
