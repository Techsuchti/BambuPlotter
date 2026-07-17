// The nanosvg implementation normally lives in the GUI library
// (BitmapCache.cpp), which this lean test target does not link. Compile it
// here for libslic3r's NSVGUtils and the plotter's SvgPlotImporter.
#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
