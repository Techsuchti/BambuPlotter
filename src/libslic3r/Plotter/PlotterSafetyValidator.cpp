#include "PlotterSafetyValidator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>

namespace Slic3r { namespace Plotter {

namespace {

std::string trim(const std::string &s)
{
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    auto begin = std::find_if_not(s.begin(), s.end(), is_space);
    auto end   = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::vector<std::string> split_words(const std::string &s)
{
    std::vector<std::string> words;
    std::istringstream       is(s);
    std::string              w;
    while (is >> w)
        words.emplace_back(w);
    return words;
}

bool parse_number(const std::string &s, double &value)
{
    if (s.empty())
        return false;
    size_t consumed = 0;
    try {
        value = std::stod(s, &consumed);
    } catch (...) {
        return false;
    }
    return consumed == s.size() && std::isfinite(value);
}

// Well-known reasons for commonly encountered prohibited commands; anything
// unrecognized still fails through the default-deny path.
const std::map<std::string, std::string> &prohibited_reasons()
{
    static const std::map<std::string, std::string> reasons = {
        {"G28", "homing is prohibited inside a plot job (it can crush a mounted pen)"},
        {"G29", "bed leveling is prohibited in plotter jobs"},
        {"G91", "relative positioning is prohibited; plotter jobs are absolute (G90) only"},
        {"G92", "resetting the coordinate system is prohibited in plotter jobs"},
        {"M82", "E-axis (extrusion) control is prohibited in plotter jobs"},
        {"M83", "E-axis (extrusion) control is prohibited in plotter jobs"},
        {"M104", "nozzle heating is prohibited in plotter jobs"},
        {"M106", "part fan control is not allowed in plotter jobs"},
        {"M109", "nozzle heating is prohibited in plotter jobs"},
        {"M140", "bed heating is prohibited in plotter jobs"},
        {"M141", "chamber heating is prohibited in plotter jobs"},
        {"M190", "bed heating is prohibited in plotter jobs"},
        {"M600", "filament change is prohibited in plotter jobs"},
        {"M620", "AMS commands are prohibited in plotter jobs"},
        {"M621", "AMS commands are prohibited in plotter jobs"},
        {"M701", "filament loading is prohibited in plotter jobs"},
        {"M702", "filament unloading is prohibited in plotter jobs"},
        {"M900", "pressure advance concerns extrusion and is prohibited in plotter jobs"},
        {"M970", "vibration/flow calibration is prohibited in plotter jobs"},
        {"M971", "vibration/flow calibration is prohibited in plotter jobs"},
        {"M974", "vibration/flow calibration is prohibited in plotter jobs"},
        {"M975", "vibration/flow calibration is prohibited in plotter jobs"},
        {"M991", "printer stage notifications are not allowed in plotter jobs"},
        {"M1002", "internal Bambu judge/state commands are prohibited in plotter jobs"},
    };
    return reasons;
}

} // namespace

std::string ValidationResult::summary() const
{
    if (ok)
        return "G-code passed plotter safety validation";
    std::ostringstream os;
    os << "G-code REJECTED by plotter safety validation (" << issues.size()
       << (issues.size() == 1 ? " issue):" : " issues):");
    for (const ValidationIssue &issue : issues)
        os << "\n  line " << issue.line_number << ": " << issue.reason
           << (issue.line.empty() ? "" : "  [" + issue.line + "]");
    return os.str();
}

ValidationResult PlotterSafetyValidator::validate(const std::string &gcode, const PlotterToolProfile &profile)
{
    ValidationResult result;

    if (const std::string reason = profile.invalid_reason(); !reason.empty()) {
        result.issues.push_back({0, {}, "plotter profile is not usable: " + reason});
        return result;
    }

    bool   seen_g90        = false;
    bool   seen_motion     = false; // any G0/G1/G28
    bool   z_known         = false;
    bool   f_known         = false;
    double last_z          = 0.;
    size_t line_number     = 0;

    auto add_issue = [&](const std::string &line, const std::string &reason) {
        if (result.issues.size() < MAX_ISSUES)
            result.issues.push_back({line_number, line, reason});
    };

    std::istringstream stream(gcode);
    std::string        raw;
    while (std::getline(stream, raw)) {
        ++line_number;
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();

        // Strip comment. Comments may hold arbitrary metadata (config block),
        // so only the executable part is checked for printable ASCII.
        std::string line = raw;
        if (const size_t semicolon = line.find(';'); semicolon != std::string::npos)
            line.resize(semicolon);
        line = trim(line);
        if (line.empty())
            continue;

        bool unprintable = false;
        for (unsigned char c : line)
            if (c != '\t' && (c < 0x20 || c > 0x7e)) {
                add_issue(line, "command contains non-printable or non-ASCII characters");
                unprintable = true;
                break;
            }
        if (unprintable)
            continue;

        std::vector<std::string> words = split_words(line);
        std::string cmd = words.front();
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::toupper(c); });

        if (cmd == "G90") {
            if (words.size() > 1)
                add_issue(line, "G90 takes no parameters");
            seen_g90 = true;
            continue;
        }

        if (cmd == "M400") {
            if (words.size() > 1)
                add_issue(line, "M400 takes no parameters");
            continue;
        }

        // Progress reporting (printer display only, no machine effect).
        if (cmd == "M73") {
            for (size_t i = 1; i < words.size(); ++i) {
                const char letter = char(std::toupper((unsigned char) words[i][0]));
                double v;
                if ((letter != 'P' && letter != 'R' && letter != 'C' && letter != 'E') ||
                    !parse_number(words[i].substr(1), v) || v < 0.)
                    add_issue(line, "M73 only accepts non-negative P/R/C/E progress parameters");
            }
            continue;
        }

        // Heaters OFF is permitted (and expected at job end): the firmware
        // preheats on its own at print start and stays warm after FINISH
        // unless the job resets the targets. Any non-zero target is rejected.
        if (cmd == "M104" || cmd == "M140") {
            double v = -1.;
            const bool is_off = words.size() == 2 &&
                                std::toupper((unsigned char) words[1][0]) == 'S' &&
                                parse_number(words[1].substr(1), v) && v == 0.;
            if (!is_off)
                add_issue(line, cmd == "M104" ? "nozzle heating is prohibited in plotter jobs (only 'M104 S0' — heater off — is allowed)"
                                              : "bed heating is prohibited in plotter jobs (only 'M140 S0' — heater off — is allowed)");
            continue;
        }

        if (cmd == "G4") {
            for (size_t i = 1; i < words.size(); ++i) {
                std::string w = words[i];
                const char letter = char(std::toupper((unsigned char) w[0]));
                double v;
                if ((letter != 'S' && letter != 'P') || !parse_number(w.substr(1), v) || v < 0.)
                    add_issue(line, "G4 only accepts non-negative S/P dwell parameters");
            }
            continue;
        }

        if (cmd == "G28") {
            if (!profile.allow_homing_in_job)
                add_issue(line, prohibited_reasons().at("G28"));
            else if (seen_motion)
                add_issue(line, "G28 is only allowed once, before any other motion");
            else if (words.size() > 1)
                add_issue(line, "only a bare G28 (full home) is allowed");
            seen_motion = true;
            continue;
        }

        if (cmd == "G0" || cmd == "G1") {
            if (!seen_g90) {
                add_issue(line, "motion before G90; plotter jobs must be absolute");
                seen_g90 = true; // report once
            }
            std::optional<double> x, y, z, f;
            for (size_t i = 1; i < words.size(); ++i) {
                const std::string &w = words[i];
                const char letter = char(std::toupper((unsigned char) w[0]));
                double v;
                if (!parse_number(w.substr(1), v)) {
                    add_issue(line, std::string("malformed parameter '") + w + "'");
                    continue;
                }
                switch (letter) {
                case 'X': x = v; break;
                case 'Y': y = v; break;
                case 'Z': z = v; break;
                case 'F': f = v; break;
                case 'E':
                    add_issue(line, "E-axis (extrusion) movement is prohibited");
                    break;
                default:
                    add_issue(line, std::string("parameter '") + letter + "' is not allowed on G0/G1 (only X/Y/Z/F)");
                    break;
                }
            }

            if (f) {
                if (*f <= 0. || *f > MAX_FEED)
                    add_issue(line, "feedrate outside (0, " + std::to_string(int(MAX_FEED)) + "] mm/min");
                else
                    f_known = true;
            }
            if ((x || y || z) && !f_known)
                add_issue(line, "motion before any feedrate (F) was set");

            if (x && (*x < profile.min_x - BOUNDS_EPSILON || *x > profile.max_x + BOUNDS_EPSILON))
                add_issue(line, "X target outside the calibrated safe limits");
            if (y && (*y < profile.min_y - BOUNDS_EPSILON || *y > profile.max_y + BOUNDS_EPSILON))
                add_issue(line, "Y target outside the calibrated safe limits");
            if (z) {
                if (*z < profile.pen_down_z - BOUNDS_EPSILON)
                    add_issue(line, "Z target below the calibrated safe pen-down height");
                else if (*z > profile.pen_up_z + BOUNDS_EPSILON)
                    add_issue(line, "Z target above the calibrated pen-up height");
                last_z  = *z;
                z_known = true;
            }
            if ((x || y) && !z_known)
                add_issue(line, "X/Y motion before the pen height (Z) was established");

            seen_motion = true;
            continue;
        }

        // Default deny.
        if (auto it = prohibited_reasons().find(cmd); it != prohibited_reasons().end())
            add_issue(line, it->second);
        else if (!cmd.empty() && cmd[0] == 'T')
            add_issue(line, "tool changes are prohibited in plotter jobs");
        else
            add_issue(line, std::string("command '") + cmd + "' is not on the plotter allowlist");
    }

    if (!seen_motion)
        result.issues.push_back({line_number, {}, "file contains no motion commands"});
    else if (!z_known)
        result.issues.push_back({line_number, {}, "file never sets the pen height (Z)"});
    else if (std::abs(last_z - profile.pen_up_z) > BOUNDS_EPSILON)
        result.issues.push_back({line_number, {}, "job must end with the pen raised to pen_up_z"});

    result.ok = result.issues.empty();
    return result;
}

} } // namespace Slic3r::Plotter
