#include "SvgPlotImporter.hpp"

#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include <nanosvg/nanosvg.h>

#include "PathFlattener.hpp"

namespace Slic3r { namespace Plotter {

namespace {

using NSVGimagePtr = std::unique_ptr<NSVGimage, void (*)(NSVGimage *)>;

// Douglas-Peucker on Vec2d, preserving first/last points.
void simplify_dp(std::vector<Vec2d> &pts, double tolerance)
{
    if (tolerance <= 0. || pts.size() < 3)
        return;
    std::vector<bool> keep(pts.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> stack{{0, pts.size() - 1}};
    while (!stack.empty()) {
        auto [first, last] = stack.back();
        stack.pop_back();
        double max_d = 0.;
        size_t idx   = first;
        const Vec2d &a = pts[first];
        const Vec2d &b = pts[last];
        const Vec2d ab = b - a;
        const double ab_len = ab.norm();
        for (size_t i = first + 1; i < last; ++i) {
            const double d = ab_len < 1e-12 ?
                (pts[i] - a).norm() :
                std::abs(ab.x() * (a.y() - pts[i].y()) - ab.y() * (a.x() - pts[i].x())) / ab_len;
            if (d > max_d) {
                max_d = d;
                idx   = i;
            }
        }
        if (max_d > tolerance) {
            keep[idx] = true;
            stack.emplace_back(first, idx);
            stack.emplace_back(idx, last);
        }
    }
    std::vector<Vec2d> out;
    out.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        if (keep[i])
            out.emplace_back(pts[i]);
    pts = std::move(out);
}

SvgImportResult import_image(const NSVGimage &image, const SvgImportOptions &options)
{
    // nanosvg scales path coordinates to the requested unit (mm at 96 dpi)
    // but leaves image width/height in pixels — convert them ourselves.
    constexpr double px_to_mm = 25.4 / 96.0;

    SvgImportResult result;
    result.width  = double(image.width) * px_to_mm;
    result.height = double(image.height) * px_to_mm;

    // Decide whether to filter on stroked shapes.
    bool any_stroked = false;
    for (const NSVGshape *shape = image.shapes; shape != nullptr; shape = shape->next)
        if ((shape->flags & NSVG_FLAGS_VISIBLE) != 0 && shape->stroke.type != NSVG_PAINT_NONE)
            any_stroked = true;
    const bool only_stroked = options.prefer_stroked && any_stroked;

    const PathFlattener flattener(options.curve_tolerance);

    for (const NSVGshape *shape = image.shapes; shape != nullptr; shape = shape->next) {
        ++result.shapes_total;
        if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0)
            continue;
        if (only_stroked && shape->stroke.type == NSVG_PAINT_NONE)
            continue;
        ++result.shapes_imported;

        for (const NSVGpath *path = shape->paths; path != nullptr; path = path->next) {
            if (path->npts < 4)
                continue;
            std::vector<Vec2d> pts;
            pts.reserve(size_t(path->npts));
            // pts layout: p0, then (c1, c2, p) per cubic segment.
            for (int i = 0; i + 3 < path->npts; i += 3) {
                const float *p = &path->pts[i * 2];
                flattener.flatten_cubic(pts,
                                        Vec2d(p[0], p[1]), Vec2d(p[2], p[3]),
                                        Vec2d(p[4], p[5]), Vec2d(p[6], p[7]));
            }

            // SVG Y-down -> paper Y-up.
            for (Vec2d &pt : pts)
                pt.y() = result.height - pt.y();

            // Drop consecutive duplicates.
            auto last = std::unique(pts.begin(), pts.end(), [](const Vec2d &a, const Vec2d &b) {
                return (a - b).norm() < 1e-6;
            });
            pts.erase(last, pts.end());

            PlotPath plot_path;
            plot_path.closed = path->closed != '\0';
            // A closed path never repeats its first point; the generator adds
            // the closing segment.
            if (plot_path.closed && pts.size() > 2 && (pts.front() - pts.back()).norm() < 1e-6)
                pts.pop_back();

            simplify_dp(pts, options.simplify_tolerance);
            plot_path.points = std::move(pts);
            if (plot_path.empty() || plot_path.length() < options.min_path_length)
                continue;
            result.paths.emplace_back(std::move(plot_path));
        }
    }

    result.ok = true;
    if (result.paths.empty()) {
        result.ok    = false;
        result.error = "the SVG contains no plottable path geometry";
    }
    return result;
}

} // namespace

SvgImportResult SvgPlotImporter::import_file(const std::string &path, const SvgImportOptions &options)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        SvgImportResult result;
        result.error = "cannot open SVG file: " + path;
        return result;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return import_memory(ss.str(), options);
}

SvgImportResult SvgPlotImporter::import_memory(const std::string &svg_text, const SvgImportOptions &options)
{
    // nsvgParse mutates its input, so hand it a writable copy.
    std::vector<char> buf(svg_text.begin(), svg_text.end());
    buf.push_back('\0');
    NSVGimagePtr image(nsvgParse(buf.data(), "mm", 96.0f), nsvgDelete);
    if (image == nullptr || image->shapes == nullptr) {
        SvgImportResult result;
        result.error = "failed to parse SVG document";
        return result;
    }
    return import_image(*image, options);
}

} } // namespace Slic3r::Plotter
