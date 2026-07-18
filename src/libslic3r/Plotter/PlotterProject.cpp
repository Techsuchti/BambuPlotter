#include "PlotterProject.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace Plotter {

using nlohmann::json;

PlotPaths PlotterProject::import_paths(std::string *error) const
{
    const SvgImportResult r = SvgPlotImporter::import_memory(svg_markup, import_options);
    if (!r.ok) {
        if (error != nullptr)
            *error = r.error;
        return {};
    }
    return r.paths;
}

PlotPaths PlotterProject::placed_paths(std::string *error) const
{
    PlotPaths paths = import_paths(error);
    transform(paths, scale, offset);
    return paths;
}

std::string PlotterProject::serialize_json() const
{
    json j;
    j["version"]         = version;
    j["source_svg_path"] = source_svg_path;
    j["svg_markup"]      = svg_markup;
    j["doc_width"]       = doc_width;
    j["doc_height"]      = doc_height;
    j["scale"]           = scale;
    j["offset"]          = { offset.x(), offset.y() };
    j["import_options"]  = {
        {"curve_tolerance", import_options.curve_tolerance},
        {"prefer_stroked", import_options.prefer_stroked},
        {"min_path_length", import_options.min_path_length},
        {"simplify_tolerance", import_options.simplify_tolerance},
    };
    return j.dump(4);
}

bool PlotterProject::deserialize_json(const std::string &json_text, std::string *error)
{
    try {
        const json j = json::parse(json_text);
        PlotterProject p;
        p.version         = j.at("version").get<int>();
        p.source_svg_path = j.value("source_svg_path", std::string());
        p.svg_markup      = j.at("svg_markup").get<std::string>();
        p.doc_width       = j.value("doc_width", 0.);
        p.doc_height      = j.value("doc_height", 0.);
        p.scale           = j.value("scale", 1.0);
        if (j.contains("offset"))
            p.offset = Vec2d(j["offset"].at(0).get<double>(), j["offset"].at(1).get<double>());
        if (j.contains("import_options")) {
            const auto &o = j["import_options"];
            p.import_options.curve_tolerance   = o.value("curve_tolerance", 0.1);
            p.import_options.prefer_stroked     = o.value("prefer_stroked", true);
            p.import_options.min_path_length    = o.value("min_path_length", 0.05);
            p.import_options.simplify_tolerance = o.value("simplify_tolerance", 0.02);
        }
        *this = p;
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

PlotterProject PlotterProject::from_svg_file(const std::string &path, const SvgImportOptions &options, std::string *error)
{
    PlotterProject p;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) *error = "cannot open SVG file: " + path;
        return p;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    p.svg_markup      = ss.str();
    p.source_svg_path = path;
    p.import_options  = options;

    const SvgImportResult r = SvgPlotImporter::import_memory(p.svg_markup, options);
    if (!r.ok) {
        if (error != nullptr) *error = r.error;
        return p;
    }
    p.doc_width  = r.width;
    p.doc_height = r.height;
    return p;
}

} } // namespace Slic3r::Plotter
