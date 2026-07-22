#ifndef slic3r_PlotterCalibrationController_hpp_
#define slic3r_PlotterCalibrationController_hpp_

#include <functional>
#include <string>

#include "libslic3r/Plotter/PlotterToolProfile.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

class MachineObject;

namespace GUI {

// Drives the manual plotter calibration on a connected A1 mini and owns the
// persistent PlotterToolProfile.
//
// Position model: the printer does not report live XYZ over MQTT (only
// home-flags), so this controller dead-reckons. After homing it commands an
// absolute move to a known park position; from then on every jog is an
// absolute G1 to a clamped target, so the commanded position is always known
// exactly. Any event that could invalidate the position (unhomed flags, a
// reconnect, an error) drops the controller back to NeedsHoming.
//
// Safety rules enforced here (independent of the G-code validator):
//  - jogs are only accepted in Ready state (homed + parked)
//  - every target is clamped to the machine envelope minus a margin
//  - Z can never go below `z_floor` (default 0.2 mm above the bed)
//  - homing is refused once the user confirms the pen is mounted; the
//    wizard's contract is: home bare -> mount pen -> calibrate -> plot
class PlotterCalibrationController
{
public:
    enum class State {
        Disconnected,   // no usable MachineObject
        NeedsHoming,    // connected, position unknown
        Homing,         // G28 sent, waiting for home flags
        Parking,        // absolute move to park sent
        Ready,          // position known, jogs allowed
    };

    // A1 mini machine envelope (mm) with conservative margins.
    static constexpr double ENVELOPE_MIN_X = 0.;
    static constexpr double ENVELOPE_MAX_X = 178.;
    static constexpr double ENVELOPE_MIN_Y = 0.;
    static constexpr double ENVELOPE_MAX_Y = 178.;
    static constexpr double ENVELOPE_MAX_Z = 170.;
    static constexpr double PARK_X = 90.;
    static constexpr double PARK_Y = 90.;
    static constexpr double PARK_Z = 30.;

    PlotterCalibrationController();

    // --- profile persistence (data_dir()/plotter/plotter_profile.json) ----
    Plotter::PlotterToolProfile       &profile() { return m_profile; }
    const Plotter::PlotterToolProfile &profile() const { return m_profile; }
    bool load_profile(std::string *error = nullptr);
    bool save_profile(std::string *error = nullptr) const;
    static std::string profile_path();

    // --- machine binding ---------------------------------------------------
    // The controller never owns the MachineObject; callers pass the current
    // selection each UI tick. State degrades to Disconnected on nullptr.
    void set_machine(MachineObject *obj);
    MachineObject *machine() const { return m_machine; }

    // Re-evaluate state from printer flags; call periodically from the UI.
    void update();

    State       state() const { return m_state; }
    std::string state_description() const;

    // Commanded (dead-reckoned) position; only meaningful in Ready state.
    const Vec3d &position() const { return m_position; }
    bool         position_known() const { return m_state == State::Ready; }

    // The wizard sets this once the pen is mounted; homing is refused after.
    void set_pen_mounted(bool mounted) { m_pen_mounted = mounted; }
    bool pen_mounted() const { return m_pen_mounted; }

    // The printer's own home flags (live telemetry). True means the firmware
    // still knows its origin - position can be re-established WITHOUT
    // homing, via an absolute park move (safe with the pen mounted).
    bool machine_reports_homed() const { return machine_homed(); }
    // Re-establish dead reckoning from a known firmware origin without G28.
    // Use when the wizard was reopened and only the app-side position was
    // lost. Requires machine_reports_homed().
    bool sync_position(std::string *error = nullptr);

    // --- verified-safe motions ----------------------------------------------
    // All return false with `error` set when refused.
    bool home(std::string *error = nullptr);
    // Relative jog request; the target is clamped to the envelope and the
    // controller's Z floor before the absolute move is sent.
    bool jog(char axis, double delta_mm, std::string *error = nullptr);
    // Absolute move (used by "go to paper origin" style buttons).
    bool move_to(const Vec3d &target, double speed_mm_s, std::string *error = nullptr);

    // --- calibration captures ----------------------------------------------
    // Each captures from the current dead-reckoned position (Ready only).
    bool capture_paper_origin(std::string *error = nullptr);
    bool capture_min_corner(std::string *error = nullptr);
    bool capture_max_corner(std::string *error = nullptr);
    bool capture_pen_contact(std::string *error = nullptr);   // sets contact + derives down/up
    bool capture_pen_up(std::string *error = nullptr);

    // Marks the profile calibrated when all fields are consistent.
    bool finalize(std::string *error = nullptr);

    // Jog speeds (mm/s).
    double xy_speed = 50.;
    double z_speed  = 8.;
    // How far below pen-contact the pen presses while drawing (mm).
    double pen_press = 0.3;
    // Z floor for jogs (absolute mm above bed).
    double z_floor = 0.2;

private:
    bool ensure_ready(std::string *error) const;
    bool send_absolute_move(const Vec3d &target, double speed_mm_s, std::string *error);
    bool machine_homed() const;

    MachineObject     *m_machine = nullptr;
    Plotter::PlotterToolProfile m_profile;
    State              m_state = State::Disconnected;
    Vec3d              m_position{0., 0., 0.};
    bool               m_pen_mounted = false;
    // Homing handshake: the printer's home flags may still be TRUE from a
    // previous session when G28 is sent, so "flags true" alone must not be
    // trusted until they were seen dropping (or a timeout passes).
    bool               m_seen_unhomed = false;
    int                m_homing_ticks = 0;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterCalibrationController_hpp_
