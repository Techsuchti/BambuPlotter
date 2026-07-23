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

SvgImportResult import_image(const NSVGimage &image, const SvgImportOptions &options)
{
    // nanosvg scales path coordinates to the requested unit (mm at 96 dpi)
    // but leaves image width/height in pixels — convert them ourselves.
    constexpr double px_to_mm = 25.4 / 96.0;

    SvgImportResult result;
    result.width  = double(image.width) * px_to_mm;
    result.height = double(image.height) * px_to_mm;

    const PathFlattener flattener(options.curve_tolerance);

    // Flattens one nanosvg path into a PlotPath (doc space, Y up, simplified).
    // Returns an empty path when the geometry is degenerate or too short.
    auto flatten_path = [&](const NSVGpath *path, bool force_closed) -> PlotPath {
        PlotPath plot_path;
        if (path->npts < 4)
            return plot_path;
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

        // SVG fills implicitly close open subpaths.
        plot_path.closed = force_closed || path->closed != '\0';
        // A closed path never repeats its first point; the generator adds
        // the closing segment.
        if (plot_path.closed && pts.size() > 2 && (pts.front() - pts.back()).norm() < 1e-6)
            pts.pop_back();

        simplify_dp(pts, options.simplify_tolerance);
        plot_path.points = std::move(pts);
        if (plot_path.empty() || plot_path.length() < options.min_path_length)
            plot_path.points.clear();
        return plot_path;
    };

    for (const NSVGshape *shape = image.shapes; shape != nullptr; shape = shape->next) {
        ++result.shapes_total;
        if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0)
            continue;
        const bool has_stroke = shape->stroke.type != NSVG_PAINT_NONE;
        const bool has_fill   = shape->fill.type != NSVG_PAINT_NONE;
        // Unpainted shapes leave no ink in any renderer; skip them.
        if (!has_stroke && !has_fill)
            continue;
        ++result.shapes_imported;

        SvgFillRegion region;
        region.even_odd = shape->fillRule == NSVG_FILLRULE_EVENODD;
        // Light fills are paper, not ink (auto-vectorizers stack white
        // shapes over black to carve out details - painter's algorithm).
        if (has_fill && shape->fill.type == NSVG_PAINT_COLOR) {
            const unsigned int c = shape->fill.color; // NSVG_RGB: r | g<<8 | b<<16
            const double luminance = 0.299 * double(c & 0xFF)
                                   + 0.587 * double((c >> 8) & 0xFF)
                                   + 0.114 * double((c >> 16) & 0xFF);
            region.erases = luminance >= 160.;
        }

        for (const NSVGpath *path = shape->paths; path != nullptr; path = path->next) {
            PlotPath outline = flatten_path(path, /*force_closed=*/!has_stroke && has_fill);
            if (outline.empty())
                continue;
            if (has_fill) {
                PlotPath contour = outline;
                contour.closed   = true;
                region.contours.emplace_back(std::move(contour));
            }
            // Stroke paint draws the path itself; fill outlines are derived
            // later from the COMPOSED ink (plot_fill_regions), so thin fills
            // can become single centerline strokes instead of double lines.
            if (has_stroke)
                result.paths.emplace_back(std::move(outline));
        }

        if (has_fill && !region.contours.empty())
            result.fill_regions.emplace_back(std::move(region));
    }

    result.ok = true;
    if (result.paths.empty() && result.fill_regions.empty()) {
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
