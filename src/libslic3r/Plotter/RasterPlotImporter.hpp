#ifndef slic3r_RasterPlotImporter_hpp_
#define slic3r_RasterPlotImporter_hpp_

#include <cstdint>
#include <string>

namespace Slic3r { namespace Plotter {

// Raster line art (PNG) -> plot artwork, without an external vectorizer.
// The bitmap is thresholded into ink vs paper and its ink boundaries traced
// (marching squares) into an even-odd filled SVG document. The result feeds
// the exact same pipeline as an imported SVG: fill composition, outlines,
// hatching, centerline strokes and density limiting all apply unchanged,
// and .bplot projects embed the traced markup like any other artwork.
struct RasterTraceOptions
{
    // Ink threshold on the 0-255 grayscale; -1 picks it automatically
    // (Otsu). Pixels at or below the threshold are ink.
    int    threshold = -1;
    // Douglas-Peucker tolerance in PIXELS applied to the traced boundaries:
    // smooths the pixel staircase into clean strokes. Zero disables.
    double simplify_px = 0.75;
    // Boundary rings enclosing less than this many square pixels are
    // dropped as scanning noise.
    double min_speck_px2 = 4.;
};

struct RasterTraceResult
{
    bool        ok = false;
    std::string error;
    std::string svg_markup;     // even-odd filled document of the ink
    size_t      width_px  = 0;  // traced image size (after any downscale)
    size_t      height_px = 0;
    int         threshold_used = 0;
    size_t      ring_count = 0; // boundary rings in the markup
    // Light-on-dark art (scratchboard, chalk, negatives): when the dark
    // side of the threshold covers most of the image, the LIGHT strokes
    // are the drawing and get traced instead - a pen cannot "plot" a
    // black background, and hatching one page-sized region with tens of
    // thousands of hairline holes runs practically forever.
    bool        inverted = false;
};

// Core tracer over an 8-bit grayscale buffer (row major, `cols` wide).
RasterTraceResult trace_gray_to_svg(const uint8_t *gray, size_t cols, size_t rows,
                                    const RasterTraceOptions &options = {});

// File entry: decodes any 8/16-bit gray / palette / RGB / RGBA PNG
// (alpha composited over white paper) and traces it. Images larger than
// 4096 px on a side are box-downscaled first - line art needs no more.
RasterTraceResult trace_png_to_svg(const std::string &png_path,
                                   const RasterTraceOptions &options = {});

} } // namespace Slic3r::Plotter

#endif // slic3r_RasterPlotImporter_hpp_
