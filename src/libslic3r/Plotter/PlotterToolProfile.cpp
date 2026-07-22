#include "PlotterToolProfile.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace Plotter {

using nlohmann::json;

std::string PlotterToolProfile::invalid_reason() const
{
    auto finite = [](double v) { return std::isfinite(v); };
    if (!calibrated)
        return "profile has not been calibrated";
    if (!finite(min_x) || !finite(max_x) || !finite(min_y) || !finite(max_y) ||
        !finite(pen_up_z) || !finite(pen_contact_z) || !finite(pen_down_z) ||
        !finite(paper_origin.x()) || !finite(paper_origin.y()) ||
        !finite(pen_offset.x()) || !finite(pen_offset.y()))
        return "profile contains non-finite values";
    if (min_x >= max_x || min_y >= max_y)
        return "safe X/Y limits are empty or inverted";
    if (pen_down_z > pen_contact_z || pen_contact_z > pen_up_z)
        return "pen Z heights must satisfy pen_down_z <= pen_contact_z <= pen_up_z";
    if (pen_down_z < 0.)
        return "pen_down_z must not be below Z=0";
    if (paper_origin.x() < min_x || paper_origin.x() > max_x ||
        paper_origin.y() < min_y || paper_origin.y() > max_y)
        return "paper origin lies outside the safe X/Y limits";
    if (travel_speed <= 0. || draw_speed <= 0. || lift_speed <= 0.)
        return "speeds must be positive";
    if (travel_speed > 300. || draw_speed > 300. || lift_speed > 50.)
        return "speeds exceed the plotter safety caps (300/300/50 mm/s)";
    return {};
}

std::string PlotterToolProfile::serialize_json() const
{
    json j;
    j["version"]             = version;
    j["calibrated"]          = calibrated;
    j["pen_offset"]          = { pen_offset.x(), pen_offset.y() };
    j["paper_origin"]        = { paper_origin.x(), paper_origin.y() };
    j["min_x"]               = min_x;
    j["max_x"]               = max_x;
    j["min_y"]               = min_y;
    j["max_y"]               = max_y;
    j["pen_up_z"]            = pen_up_z;
    j["pen_contact_z"]       = pen_contact_z;
    j["pen_down_z"]          = pen_down_z;
    j["travel_speed"]        = travel_speed;
    j["draw_speed"]          = draw_speed;
    j["lift_speed"]          = lift_speed;
    j["pen_tip_width"]       = pen_tip_width;
    j["allow_homing_in_job"] = allow_homing_in_job;
    return j.dump(4);
}

bool PlotterToolProfile::deserialize_json(const std::string &json_text, std::string *error)
{
    try {
        const json j = json::parse(json_text);
        PlotterToolProfile p;
        p.version             = j.at("version").get<int>();
        p.calibrated          = j.at("calibrated").get<bool>();
        p.pen_offset          = Vec2d(j.at("pen_offset").at(0).get<double>(),
                                      j.at("pen_offset").at(1).get<double>());
        p.paper_origin        = Vec2d(j.at("paper_origin").at(0).get<double>(),
                                      j.at("paper_origin").at(1).get<double>());
        p.min_x               = j.at("min_x").get<double>();
        p.max_x               = j.at("max_x").get<double>();
        p.min_y               = j.at("min_y").get<double>();
        p.max_y               = j.at("max_y").get<double>();
        p.pen_up_z            = j.at("pen_up_z").get<double>();
        p.pen_contact_z       = j.at("pen_contact_z").get<double>();
        p.pen_down_z          = j.at("pen_down_z").get<double>();
        p.travel_speed        = j.at("travel_speed").get<double>();
        p.draw_speed          = j.at("draw_speed").get<double>();
        p.lift_speed          = j.at("lift_speed").get<double>();
        p.pen_tip_width       = j.value("pen_tip_width", 0.5);
        p.allow_homing_in_job = j.value("allow_homing_in_job", false);
        *this = p;
        return true;
    } catch (const std::exception &e) {
        if (error != nullptr)
            *error = e.what();
        return false;
    }
}

bool PlotterToolProfile::save(const std::string &path, std::string *error) const
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error != nullptr)
            *error = "cannot open file for writing: " + path;
        return false;
    }
    out << serialize_json();
    out.close();
    if (!out) {
        if (error != nullptr)
            *error = "failed writing file: " + path;
        return false;
    }
    return true;
}

bool PlotterToolProfile::load(const std::string &path, std::string *error)
{
    std::ifstream in(path);
    if (!in) {
        if (error != nullptr)
            *error = "cannot open file for reading: " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return deserialize_json(ss.str(), error);
}

} } // namespace Slic3r::Plotter
