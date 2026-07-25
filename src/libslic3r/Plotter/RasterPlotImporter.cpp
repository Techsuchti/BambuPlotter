#include "RasterPlotImporter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include <png.h>

#include "PlotterPath.hpp"
#include "libslic3r/MarchingSquares.hpp"

namespace Slic3r { namespace Plotter {

// Binary ink grid for the marching squares tracer: ink = 255, paper = 0,
// padded with one paper pixel on every side so ink touching the image
// border still closes into a ring.
struct RasterInkGrid
{
    std::vector<uint8_t> cells;
    size_t               rows = 0, cols = 0;
};

} } // namespace Slic3r::Plotter

namespace marchsq {

template<> struct _RasterTraits<Slic3r::Plotter::RasterInkGrid>
{
    using Rst       = Slic3r::Plotter::RasterInkGrid;
    using ValueType = uint8_t;

    static uint8_t get(const Rst &rst, size_t row, size_t col) { return rst.cells[row * rst.cols + col]; }
    static size_t  rows(const Rst &rst) { return rst.rows; }
    static size_t  cols(const Rst &rst) { return rst.cols; }
};

} // namespace marchsq

namespace Slic3r { namespace Plotter {

namespace {

int otsu_threshold(const std::vector<uint8_t> &gray)
{
    std::array<double, 256> hist{};
    for (uint8_t v : gray)
        hist[v] += 1.;
    const double total = double(gray.size());

    double sum = 0.;
    for (int i = 0; i < 256; ++i)
        sum += i * hist[i];

    double sum_b = 0., w_b = 0., best_var = -1.;
    int    best = 127;
    for (int t = 0; t < 256; ++t) {
        w_b += hist[t];
        if (w_b <= 0.)
            continue;
        const double w_f = total - w_b;
        if (w_f <= 0.)
            break;
        sum_b += t * hist[t];
        const double m_b = sum_b / w_b;
        const double m_f = (sum - sum_b) / w_f;
        const double var = w_b * w_f * (m_b - m_f) * (m_b - m_f);
        if (var > best_var) {
            best_var = var;
            best     = t;
        }
    }
    return best;
}

double ring_area_px2(const std::vector<Vec2d> &ring)
{
    double a = 0.;
    for (size_t i = 0; i < ring.size(); ++i) {
        const Vec2d &p = ring[i];
        const Vec2d &q = ring[(i + 1) % ring.size()];
        a += p.x() * q.y() - q.x() * p.y();
    }
    return std::abs(0.5 * a);
}

// Decodes any PNG variant into 8-bit RGBA via the standard libpng
// transform recipe (the in-tree decode helpers only accept plain
// gray8/RGB/RGBA, but downloaded line art is very often palette-coded).
bool decode_png_rgba(const std::string &path, std::vector<uint8_t> &rgba, size_t &cols, size_t &rows, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open file";
        return false;
    }
    std::vector<uint8_t> file_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file_bytes.size() < 8 || png_sig_cmp(file_bytes.data(), 0, 8) != 0) {
        error = "not a PNG file";
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop   info = png ? png_create_info_struct(png) : nullptr;
    if (info == nullptr) {
        if (png != nullptr)
            png_destroy_read_struct(&png, nullptr, nullptr);
        error = "libpng init failed";
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        error = "corrupt PNG data";
        return false;
    }

    struct Cursor { const uint8_t *data; size_t size, pos; } cursor{file_bytes.data(), file_bytes.size(), 0};
    png_set_read_fn(png, &cursor, [](png_structp p, png_bytep out, png_size_t n) {
        auto *c = static_cast<Cursor *>(png_get_io_ptr(p));
        if (c->pos + n > c->size)
            png_error(p, "unexpected end of PNG data");
        std::copy(c->data + c->pos, c->data + c->pos + n, out);
        c->pos += n;
    });

    png_read_info(png, info);
    cols = png_get_image_width(png, info);
    rows = png_get_image_height(png, info);

    // Normalize every color type / bit depth to RGBA8.
    png_set_strip_16(png);
    png_set_packing(png);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(png, info) < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY ||
        png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);

    rgba.resize(rows * cols * 4);
    std::vector<png_bytep> row_ptrs(rows);
    for (size_t r = 0; r < rows; ++r)
        row_ptrs[r] = rgba.data() + r * cols * 4;
    png_read_image(png, row_ptrs.data());
    png_destroy_read_struct(&png, &info, nullptr);
    return true;
}

} // namespace

RasterTraceResult trace_gray_to_svg(const uint8_t *gray, size_t cols, size_t rows, const RasterTraceOptions &options)
{
    RasterTraceResult res;
    if (gray == nullptr || cols < 2 || rows < 2) {
        res.error = "image too small";
        return res;
    }
    res.width_px  = cols;
    res.height_px = rows;

    std::vector<uint8_t> pixels(gray, gray + cols * rows);
    res.threshold_used = options.threshold >= 0 ? std::min(options.threshold, 255) : otsu_threshold(pixels);

    // Which side of the threshold is the drawing? Dark strokes on light
    // paper normally - but when the dark side covers most of the image
    // (scratchboard / negative art), the light strokes are the drawing.
    size_t dark_px = 0;
    for (uint8_t v : pixels)
        if (v <= res.threshold_used)
            ++dark_px;
    res.inverted = dark_px * 2 > pixels.size();

    RasterInkGrid grid;
    grid.cols = cols + 2;
    grid.rows = rows + 2;
    grid.cells.assign(grid.cols * grid.rows, 0);
    size_t ink_px = 0;
    for (size_t r = 0; r < rows; ++r)
        for (size_t c = 0; c < cols; ++c) {
            const bool dark = pixels[r * cols + c] <= res.threshold_used;
            if (dark != res.inverted) {
                grid.cells[(r + 1) * grid.cols + (c + 1)] = 255;
                ++ink_px;
            }
        }
    if (ink_px == 0) {
        res.error = "no ink found - the image has no pixels darker than the threshold";
        return res;
    }

    std::vector<marchsq::Ring> rings = marchsq::execute(grid, 128, {2, 2});

    std::ostringstream d;
    d.setf(std::ios::fixed);
    d.precision(1);
    for (const marchsq::Ring &ring : rings) {
        if (ring.size() < 3)
            continue;
        std::vector<Vec2d> pts;
        pts.reserve(ring.size() + 1);
        for (const marchsq::Coord &crd : ring)
            pts.emplace_back(double(crd.c) - 1., double(crd.r) - 1.); // un-pad
        if (options.simplify_px > 0.) {
            // Close the ring for simplification so the seam stays anchored,
            // then reopen it.
            pts.emplace_back(pts.front());
            simplify_dp(pts, options.simplify_px);
            pts.pop_back();
        }
        if (pts.size() < 3 || ring_area_px2(pts) < options.min_speck_px2)
            continue;
        d << "M" << pts[0].x() << " " << pts[0].y();
        for (size_t i = 1; i < pts.size(); ++i)
            d << "L" << pts[i].x() << " " << pts[i].y();
        d << "Z";
        ++res.ring_count;
    }
    if (res.ring_count == 0) {
        res.error = "no ink shapes survived tracing";
        return res;
    }

    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << cols << "\" height=\"" << rows
        << "\" viewBox=\"0 0 " << cols << " " << rows << "\">"
        << "<path fill=\"#000000\" fill-rule=\"evenodd\" d=\"" << d.str() << "\"/></svg>";
    res.svg_markup = svg.str();
    res.ok         = true;
    return res;
}

RasterTraceResult trace_png_to_svg(const std::string &png_path, const RasterTraceOptions &options)
{
    RasterTraceResult    res;
    std::vector<uint8_t> rgba;
    size_t               cols = 0, rows = 0;
    if (!decode_png_rgba(png_path, rgba, cols, rows, res.error))
        return res;

    // Luminance, alpha composited over white paper.
    std::vector<uint8_t> gray(cols * rows);
    for (size_t i = 0; i < gray.size(); ++i) {
        const uint8_t *px  = &rgba[i * 4];
        const double   lum = 0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2];
        gray[i] = uint8_t(std::lround((px[3] * lum + (255 - px[3]) * 255.) / 255.));
    }

    // Box-downscale oversized photos; the tracer needs edges, not megapixels.
    while (cols > 4096 || rows > 4096) {
        const size_t         nc = cols / 2, nr = rows / 2;
        std::vector<uint8_t> half(nc * nr);
        for (size_t r = 0; r < nr; ++r)
            for (size_t c = 0; c < nc; ++c) {
                const size_t o = 2 * r * cols + 2 * c;
                half[r * nc + c] = uint8_t((int(gray[o]) + gray[o + 1] + gray[o + cols] + gray[o + cols + 1]) / 4);
            }
        gray = std::move(half);
        cols = nc;
        rows = nr;
    }

    return trace_gray_to_svg(gray.data(), cols, rows, options);
}

} } // namespace Slic3r::Plotter
