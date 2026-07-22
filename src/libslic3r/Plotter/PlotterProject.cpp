#include "PlotterProject.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace Plotter {

using nlohmann::json;

PlotPaths ProjectArtwork::placed_paths_bed(std::string *error) const
{
    PlotPaths paths = info.import_paths(error);
    const double c = std::cos(placement.rotation_z);
    const double s = std::sin(placement.rotation_z);
    for (PlotPath &p : paths)
        for (Vec2d &pt : p.points) {
            const Vec2d local = (pt - info.pivot) * placement.scale;
            pt = Vec2d(c * local.x() - s * local.y(),
                       s * local.x() + c * local.y()) + placement.offset;
        }
    return paths;
}

std::string PlotterProject::serialize_json() const
{
    json j;
    j["version"]  = version;
    j["artworks"] = json::array();
    for (const ProjectArtwork &a : artworks) {
        json ja;
        ja["name"]            = a.name;
        ja["source_svg_path"] = a.info.source_svg_path;
        ja["svg_markup"]      = a.info.svg_markup;
        ja["doc_width"]       = a.info.doc_width;
        ja["doc_height"]      = a.info.doc_height;
        ja["pivot"]           = {a.info.pivot.x(), a.info.pivot.y()};
        ja["import_options"]  = {
            {"curve_tolerance", a.info.options.curve_tolerance},
            {"prefer_stroked", a.info.options.prefer_stroked},
            {"min_path_length", a.info.options.min_path_length},
            {"simplify_tolerance", a.info.options.simplify_tolerance},
        };
        ja["placement"] = {
            {"offset", {a.placement.offset.x(), a.placement.offset.y()}},
            {"rotation_z", a.placement.rotation_z},
            {"scale", a.placement.scale},
        };
        j["artworks"].push_back(std::move(ja));
    }
    return j.dump(4);
}

bool PlotterProject::deserialize_json(const std::string &json_text, std::string *error)
{
    try {
        const json j = json::parse(json_text);
        PlotterProject p;
        p.version = j.at("version").get<int>();
        if (p.version != FORMAT_VERSION) {
            if (error != nullptr)
                *error = "unsupported .bplot version " + std::to_string(p.version) +
                         " (this build reads version " + std::to_string(int(FORMAT_VERSION)) + ")";
            return false;
        }
        for (const json &ja : j.at("artworks")) {
            ProjectArtwork a;
            a.name                 = ja.value("name", std::string());
            a.info.source_svg_path = ja.value("source_svg_path", std::string());
            a.info.svg_markup      = ja.at("svg_markup").get<std::string>();
            a.info.doc_width       = ja.value("doc_width", 0.);
            a.info.doc_height      = ja.value("doc_height", 0.);
            if (ja.contains("pivot"))
                a.info.pivot = Vec2d(ja["pivot"].at(0).get<double>(), ja["pivot"].at(1).get<double>());
            if (ja.contains("import_options")) {
                const auto &o = ja["import_options"];
                a.info.options.curve_tolerance    = o.value("curve_tolerance", 0.1);
                a.info.options.prefer_stroked     = o.value("prefer_stroked", true);
                a.info.options.min_path_length    = o.value("min_path_length", 0.05);
                a.info.options.simplify_tolerance = o.value("simplify_tolerance", 0.02);
            }
            if (ja.contains("placement")) {
                const auto &pl = ja["placement"];
                if (pl.contains("offset"))
                    a.placement.offset = Vec2d(pl["offset"].at(0).get<double>(), pl["offset"].at(1).get<double>());
                a.placement.rotation_z = pl.value("rotation_z", 0.);
                a.placement.scale      = pl.value("scale", 1.);
            }
            p.artworks.emplace_back(std::move(a));
        }
        *this = std::move(p);
        return true;
    } catch (const std::exception &e) {
        if (error != nullptr)
            *error = e.what();
        return false;
    }
}

bool PlotterProject::save(const std::string &path, std::string *error) const
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error != nullptr) *error = "cannot open file for writing: " + path;
        return false;
    }
    out << serialize_json();
    return bool(out);
}

bool PlotterProject::load(const std::string &path, std::string *error)
{
    std::ifstream in(path);
    if (!in) {
        if (error != nullptr) *error = "cannot open file for reading: " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return deserialize_json(ss.str(), error);
}

} } // namespace Slic3r::Plotter
