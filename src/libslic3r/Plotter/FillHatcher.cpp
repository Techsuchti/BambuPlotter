#include "FillHatcher.hpp"

#include <cmath>
#include <limits>
#include <map>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r { namespace Plotter {

namespace {

// Assemble one region's contours into polygons-with-holes. Fill-rule
// selection per region:
//  - declared evenodd                       -> even-odd nesting
//  - nonzero with MIXED winding directions  -> true nonzero (winding is
//    meaningful hole encoding)
//  - nonzero with UNIFORM winding           -> even-odd nesting (uniform
//    winding cannot express holes under nonzero; nesting alternation
//    preserves what the author saw).
ExPolygons region_to_expolygons(const SvgFillRegion &region)
{
    Polygons polys;
    int ccw = 0, cw = 0;
    for (const PlotPath &contour : region.contours) {
        if (contour.points.size() < 3)
            continue;
        Polygon pg;
        pg.points.reserve(contour.points.size());
        for (const Vec2d &pt : contour.points)
            pg.points.emplace_back(Point::new_scale(pt));
        (pg.is_counter_clockwise() ? ccw : cw)++;
        polys.emplace_back(std::move(pg));
    }
    const bool even_odd = region.even_odd || ccw == 0 || cw == 0;
    return union_ex(polys, even_odd ? ClipperLib::pftEvenOdd : ClipperLib::pftNonZero);
}

// Compose regions in document order (SVG painter's algorithm): dark fills
// add ink, light fills take it away - that is how auto-vectorizers carve
// white details out of black shapes.
ExPolygons compose_ink(const std::vector<SvgFillRegion> &regions)
{
    ExPolygons ink;
    for (const SvgFillRegion &region : regions) {
        ExPolygons r = region_to_expolygons(region);
        if (r.empty())
            continue;
        ink = region.erases ? diff_ex(ink, r) : union_ex(ink, r);
    }
    return ink;
}

void append_plot_path(PlotPaths &out, std::vector<Vec2d> &&pts, bool closed, double min_length)
{
    PlotPath p;
    p.closed = closed;
    p.points = std::move(pts);
    if (!p.empty() && p.length() >= min_length)
        out.emplace_back(std::move(p));
}

void hatch_lines(const ExPolygons &areas, const HatchParams &params, PlotPaths &out)
{
    BoundingBox bbox;
    for (const ExPolygon &ex : areas)
        bbox.merge(get_extents(ex));
    if (!bbox.defined)
        return;

    const double angle = params.angle_deg * M_PI / 180.;
    const Vec2d  dir(std::cos(angle), std::sin(angle));
    const Vec2d  normal(-dir.y(), dir.x());
    const Vec2d  center = unscale(bbox.center());
    // Half-diagonal covers the whole region at any angle (computed in
    // unscaled doubles - integer-vector norms truncate).
    const Vec2d  size = unscale(bbox.max) - unscale(bbox.min);
    const double span = 0.5 * size.norm() + params.spacing;
    const int    n    = int(std::ceil(span / params.spacing));

    Polylines grid;
    grid.reserve(size_t(2 * n + 1));
    for (int k = -n; k <= n; ++k) {
        const Vec2d base = center + normal * (double(k) * params.spacing);
        Polyline    line;
        line.points.emplace_back(Point::new_scale(Vec2d(base - dir * span)));
        line.points.emplace_back(Point::new_scale(Vec2d(base + dir * span)));
        grid.emplace_back(std::move(line));
    }

    // Serpentine chaining: adjacent scanline segments are joined with a
    // short DRAWN connector whenever it stays inside the filled region
    // (invisible there - the area gets inked anyway). This turns thousands
    // of individual stabs (each with a pen lift+lower cycle) into a few
    // continuous zigzag strokes; lifts are what dominate plot time.
    struct Seg
    {
        Vec2d a, b;   // a = lower coordinate along the hatch direction
        bool  used = false;
    };
    const double max_connector = 3.0 * params.spacing;

    for (const ExPolygon &ex : areas) {
        std::map<int, std::vector<Seg>> buckets;
        for (const Polyline &pl : intersection_pl(grid, ex)) {
            if (pl.points.size() < 2)
                continue;
            Vec2d a = unscale(pl.points.front());
            Vec2d b = unscale(pl.points.back());
            if ((a - center).dot(dir) > (b - center).dot(dir))
                std::swap(a, b);
            const Vec2d mid = (a + b) * 0.5;
            const int   k   = int(std::lround((mid - center).dot(normal) / params.spacing));
            buckets[k].push_back({a, b});
        }
        for (auto &bucket : buckets)
            std::sort(bucket.second.begin(), bucket.second.end(),
                      [&](const Seg &s1, const Seg &s2) { return (s1.a - center).dot(dir) < (s2.a - center).dot(dir); });

        // A connector is drawable when it verifiably stays in the region:
        // the exact segment test, or (for chords lying on straight walls)
        // sampled points, borders counting as inside.
        auto connector_ok = [&](const Vec2d &from, const Vec2d &to) -> bool {
            const double len = (to - from).norm();
            if (len > max_connector)
                return false;
            const Point pf = Point::new_scale(from), pt = Point::new_scale(to);
            if (ex.contains(Line(pf, pt)))
                return true;
            // Fallback for chords hugging straight walls: sample densely so
            // the connector cannot slip across hair-thin white details.
            const int steps = std::max(3, int(std::ceil(len / 0.25)));
            for (int i = 1; i < steps; ++i)
                if (!ex.contains(Point::new_scale(Vec2d(from + (to - from) * (double(i) / steps)))))
                    return false;
            return true;
        };

        size_t remaining = 0;
        for (const auto &bucket : buckets)
            remaining += bucket.second.size();

        while (remaining > 0) {
            // Start a chain at the first unused segment (lowest scanline).
            std::vector<Vec2d> chain;
            int chain_k = 0;
            for (auto &bucket : buckets) {
                for (Seg &seg : bucket.second)
                    if (!seg.used) {
                        seg.used = true;
                        --remaining;
                        chain   = {seg.a, seg.b};
                        chain_k = bucket.first;
                        break;
                    }
                if (!chain.empty())
                    break;
            }

            // Extend into successive scanlines while a valid connector exists.
            while (true) {
                auto it = buckets.find(chain_k + 1);
                if (it == buckets.end())
                    break;
                const Vec2d cur = chain.back();
                Seg  *best      = nullptr;
                bool  enter_at_a = true;
                double best_d    = std::numeric_limits<double>::max();
                for (Seg &seg : it->second) {
                    if (seg.used)
                        continue;
                    const double da = (seg.a - cur).norm();
                    const double db = (seg.b - cur).norm();
                    const double d  = std::min(da, db);
                    if (d < best_d) {
                        best_d     = d;
                        best       = &seg;
                        enter_at_a = da <= db;
                    }
                }
                if (best == nullptr)
                    break;
                const Vec2d entry = enter_at_a ? best->a : best->b;
                const Vec2d exit  = enter_at_a ? best->b : best->a;
                if (!connector_ok(cur, entry))
                    break;
                best->used = true;
                --remaining;
                chain.emplace_back(entry);
                chain.emplace_back(exit);
                ++chain_k;
            }

            append_plot_path(out, std::move(chain), false, params.min_length);
        }
    }
}

void hatch_concentric(const ExPolygons &areas, const HatchParams &params, PlotPaths &out)
{
    const float delta = -float(scale_(params.spacing));
    ExPolygons  current = areas;
    // Hard iteration cap: a 200 mm region at 0.1 mm spacing is 1000 rings.
    for (int guard = 0; guard < 5000 && !current.empty(); ++guard) {
        current = offset_ex(current, delta);
        for (const ExPolygon &ex : current) {
            auto emit_ring = [&](const Polygon &ring) {
                std::vector<Vec2d> pts;
                pts.reserve(ring.points.size());
                for (const Point &pt : ring.points)
                    pts.emplace_back(unscale(pt));
                append_plot_path(out, std::move(pts), true, params.min_length);
            };
            emit_ring(ex.contour);
            for (const Polygon &hole : ex.holes)
                emit_ring(hole);
        }
    }
}

} // namespace

PlotPaths hatch_fill_regions(const std::vector<SvgFillRegion> &regions, const HatchParams &params)
{
    PlotPaths out;
    if (regions.empty() || params.spacing < 0.01)
        return out;

    // Painter's-order composition, then shrink by the pen half-width.
    ExPolygons merged = compose_ink(regions);
    if (params.inset > 0.005)
        merged = offset_ex(merged, -float(scale_(params.inset)));
    if (merged.empty())
        return out;

    if (params.pattern == HatchPattern::Concentric)
        hatch_concentric(merged, params, out);
    else
        hatch_lines(merged, params, out);
    return out;
}

} } // namespace Slic3r::Plotter
