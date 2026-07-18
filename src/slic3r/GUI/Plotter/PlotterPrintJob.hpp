#ifndef slic3r_PlotterPrintJob_hpp_
#define slic3r_PlotterPrintJob_hpp_

#include <functional>
#include <string>

#include "libslic3r/Plotter/PlotterJobBuilder.hpp"

namespace Slic3r {

class MachineObject;

namespace GUI {

// Uploads a built plotter job to a LAN-connected A1 mini and starts it, using
// the exact sequence verified on hardware:
//   1. write the container to a temp .gcode.3mf
//   2. NetworkAgent::start_local_print (FTPS upload + MQTT project_file)
//
// This is deliberately NOT built on PlaterJob: a plotter job has no slicing,
// no plate model and no AMS. PrintParams is populated with every calibration
// and AMS option OFF, connection forced to LAN, so the printer never runs the
// print pipeline's calibration steps beyond the firmware's own (unavoidable,
// harmless ~75 C) prep preheat. The generated G-code ends with M104 S0/M140
// S0 so the nozzle cools after the job (see resources/plotter/README.md).
struct PlotterPrintResult
{
    bool        ok = false;
    std::string error;
};

// Progress callback: (stage_text, percent 0-100). May be null.
using PlotterPrintProgressFn = std::function<void(const std::string &, int)>;

class PlotterPrintJob
{
public:
    // Blocking: returns once the agent reports the job started (or fails).
    // `obj` must be a LAN-connected machine with a valid access code.
    static PlotterPrintResult upload_and_start(MachineObject               *obj,
                                               const Plotter::PlotterJob   &job,
                                               const PlotterPrintProgressFn &progress = nullptr);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterPrintJob_hpp_
