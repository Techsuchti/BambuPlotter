#include "PlotterCalibrationController.hpp"

#include <algorithm>
#include <cmath>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevAxis.h"

namespace Slic3r { namespace GUI {

namespace {

std::string fmt3(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

void set_error(std::string *error, const std::string &msg)
{
    if (error != nullptr)
        *error = msg;
}

} // namespace

PlotterCalibrationController::PlotterCalibrationController() = default;

std::string PlotterCalibrationController::profile_path()
{
    return data_dir() + "/plotter/plotter_profile.json";
}

bool PlotterCalibrationController::load_profile(std::string *error)
{
    return m_profile.load(profile_path(), error);
}

bool PlotterCalibrationController::save_profile(std::string *error) const
{
    boost::system::error_code ec;
    boost::filesystem::create_directories(boost::filesystem::path(profile_path()).parent_path(), ec);
    return m_profile.save(profile_path(), error);
}

void PlotterCalibrationController::set_machine(MachineObject *obj)
{
    if (obj != m_machine) {
        m_machine = obj;
        // Position knowledge never survives a machine change.
        m_state = (m_machine == nullptr) ? State::Disconnected : State::NeedsHoming;
    }
}

bool PlotterCalibrationController::machine_homed() const
{
    if (m_machine == nullptr || m_machine->GetAxis() == nullptr)
        return false;
    const auto &axis = *m_machine->GetAxis();
    return axis.IsAxisAtHomeX() && axis.IsAxisAtHomeY() && axis.IsAxisAtHomeZ();
}

void PlotterCalibrationController::update()
{
    if (m_machine == nullptr) {
        m_state = State::Disconnected;
        return;
    }
    switch (m_state) {
    case State::Disconnected:
        m_state = State::NeedsHoming;
        break;
    case State::Homing:
        // The home flags can still read TRUE from before this G28 (stale
        // telemetry / previously homed machine). Only trust them after they
        // were seen dropping while the axes home - or, if they never drop
        // within the window (report interval missed the transient), fall
        // back to a timeout well beyond a full homing cycle.
        if (!machine_homed()) {
            m_seen_unhomed = true;
        } else if (m_seen_unhomed || ++m_homing_ticks > 90 /* x500ms = 45s */) {
            // Homed for real; establish a known position via an absolute
            // park move, after which dead reckoning is valid.
            std::string err;
            if (send_absolute_move(Vec3d(PARK_X, PARK_Y, PARK_Z), xy_speed, &err))
                m_state = State::Parking;
            else
                m_state = State::NeedsHoming;
        }
        break;
    case State::Parking:
        // The park move is absolute: once sent, the commanded position IS the
        // park position (M400 in the move drains the queue before new moves).
        m_position = Vec3d(PARK_X, PARK_Y, PARK_Z);
        m_state    = State::Ready;
        break;
    case State::NeedsHoming:
    case State::Ready:
        if (m_state == State::Ready && !machine_homed())
            m_state = State::NeedsHoming; // lost position (power cycle, error)
        break;
    }
}

std::string PlotterCalibrationController::state_description() const
{
    switch (m_state) {
    case State::Disconnected: return "No printer connected";
    case State::NeedsHoming:  return "Position unknown — home the printer (remove the pen first!)";
    case State::Homing:       return "Homing…";
    case State::Parking:      return "Moving to park position…";
    case State::Ready:        return "Ready — position known";
    }
    return {};
}

bool PlotterCalibrationController::home(std::string *error)
{
    if (m_machine == nullptr) {
        set_error(error, "no printer connected");
        return false;
    }
    if (m_pen_mounted) {
        // On the A1 mini, Z homing presses the bed against the toolhead; a
        // side-mounted pen whose tip sits below the nozzle would be crushed.
        set_error(error, "homing is disabled while the pen is mounted — remove the pen first");
        return false;
    }
    if (m_machine->GetAxis() == nullptr) {
        set_error(error, "printer axis interface unavailable");
        return false;
    }
    m_machine->GetAxis()->Ctrl_GoHome();
    m_seen_unhomed = false;
    m_homing_ticks = 0;
    m_state        = State::Homing;
    return true;
}

bool PlotterCalibrationController::sync_position(std::string *error)
{
    if (m_machine == nullptr) {
        set_error(error, "no printer connected");
        return false;
    }
    if (!machine_homed()) {
        set_error(error, "the printer lost its origin (power cycle?) - it must be homed (remove the pen first)");
        return false;
    }
    // Absolute park move: the firmware knows its origin, so after this the
    // commanded position IS the position. Park Z keeps a mounted pen tip
    // well above the bed.
    std::string err;
    if (!send_absolute_move(Vec3d(PARK_X, PARK_Y, PARK_Z), xy_speed, &err)) {
        set_error(error, err);
        return false;
    }
    m_state = State::Parking;
    return true;
}

bool PlotterCalibrationController::ensure_ready(std::string *error) const
{
    if (m_machine == nullptr) {
        set_error(error, "no printer connected");
        return false;
    }
    if (m_state != State::Ready) {
        set_error(error, "position unknown — home and park first");
        return false;
    }
    return true;
}

bool PlotterCalibrationController::send_absolute_move(const Vec3d &target, double speed_mm_s, std::string *error)
{
    if (m_machine == nullptr) {
        set_error(error, "no printer connected");
        return false;
    }
    const int feed = std::max(1, int(std::lround(speed_mm_s * 60.)));
    // M400 first so this move queues after anything in flight; absolute mode
    // is stated explicitly every time (never rely on modal state).
    const std::string gcode = (boost::format("M400\nG90\nG1 X%1% Y%2% Z%3% F%4%\nM400\n")
                               % fmt3(target.x()) % fmt3(target.y()) % fmt3(target.z()) % feed).str();
    if (m_machine->publish_gcode(gcode) != 0) {
        set_error(error, "failed to send move command");
        return false;
    }
    return true;
}

bool PlotterCalibrationController::move_to(const Vec3d &target, double speed_mm_s, std::string *error)
{
    if (!ensure_ready(error))
        return false;
    Vec3d t = target;
    t.x() = std::clamp(t.x(), ENVELOPE_MIN_X, ENVELOPE_MAX_X);
    t.y() = std::clamp(t.y(), ENVELOPE_MIN_Y, ENVELOPE_MAX_Y);
    t.z() = std::clamp(t.z(), z_floor, ENVELOPE_MAX_Z);
    if (!send_absolute_move(t, speed_mm_s, error))
        return false;
    m_position = t;
    return true;
}

bool PlotterCalibrationController::jog(char axis, double delta_mm, std::string *error)
{
    if (!ensure_ready(error))
        return false;
    if (std::abs(delta_mm) > 20.) {
        set_error(error, "jog increment too large (max 20 mm)");
        return false;
    }
    Vec3d target = m_position;
    double speed = xy_speed;
    switch (std::toupper((unsigned char) axis)) {
    case 'X': target.x() += delta_mm; break;
    case 'Y': target.y() += delta_mm; break;
    case 'Z': target.z() += delta_mm; speed = z_speed; break;
    default:
        set_error(error, "unknown axis");
        return false;
    }
    return move_to(target, speed, error);
}

bool PlotterCalibrationController::capture_paper_origin(std::string *error)
{
    if (!ensure_ready(error))
        return false;
    m_profile.paper_origin = Vec2d(m_position.x(), m_position.y());
    return true;
}

bool PlotterCalibrationController::capture_min_corner(std::string *error)
{
    if (!ensure_ready(error))
        return false;
    m_profile.min_x = m_position.x();
    m_profile.min_y = m_position.y();
    return true;
}

bool PlotterCalibrationController::capture_max_corner(std::string *error)
{
    if (!ensure_ready(error))
        return false;
    m_profile.max_x = m_position.x();
    m_profile.max_y = m_position.y();
    return true;
}

bool PlotterCalibrationController::capture_pen_contact(std::string *error)
{
    if (!ensure_ready(error))
        return false;
    m_profile.pen_contact_z = m_position.z();
    m_profile.pen_down_z    = std::max(z_floor, m_position.z() - pen_press);
    if (m_profile.pen_up_z < m_profile.pen_contact_z)
        m_profile.pen_up_z = m_profile.pen_contact_z + 5.;
    return true;
}

bool PlotterCalibrationController::capture_pen_up(std::string *error)
{
    if (!ensure_ready(error))
        return false;
    m_profile.pen_up_z = m_position.z();
    return true;
}

bool PlotterCalibrationController::finalize(std::string *error)
{
    m_profile.calibrated = true;
    const std::string reason = m_profile.invalid_reason();
    if (!reason.empty()) {
        m_profile.calibrated = false;
        set_error(error, "calibration incomplete: " + reason);
        return false;
    }
    return save_profile(error);
}

} } // namespace Slic3r::GUI
