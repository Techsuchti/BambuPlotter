#include "FillHatcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>

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

namespace {

void hatch_areas(const ExPolygons &areas, const HatchParams &params, PlotPaths &out)
{
    ExPolygons interior = areas;
    if (params.inset > 0.005)
        interior = offset_ex(interior, -float(scale_(params.inset)));
    if (interior.empty())
        return;
    if (params.pattern == HatchPattern::Concentric)
        hatch_concentric(interior, params, out);
    else
        hatch_lines(interior, params, out);
}

// Occupancy raster of the keep-out zone around ink deposited so far - the
// cheap way to ask "would this stroke mostly land on paper that is already
// black?". Every accepted stroke claims a keep-out disc along its spine;
// cell size tracks the pen so the answer stays sharp for any tip.
class InkRaster
{
public:
    InkRaster(const Vec2d &bb_min, const Vec2d &bb_max, double pen_width, double keep_out)
    {
        m_res = std::min(std::max(pen_width / 6., 0.05), 0.15);
        m_min = bb_min - Vec2d(keep_out + m_res, keep_out + m_res);
        const Vec2d span = bb_max + Vec2d(keep_out + m_res, keep_out + m_res) - m_min;
        m_w = std::max(1, int(std::ceil(span.x() / m_res)));
        m_h = std::max(1, int(std::ceil(span.y() / m_res)));
        // Bound memory for absurd documents; a coarser grid only makes the
        // limiter slightly more eager.
        while (double(m_w) * double(m_h) > 32e6) {
            m_res *= 2.;
            m_w = (m_w + 1) / 2;
            m_h = (m_h + 1) / 2;
        }
        m_cells.assign(size_t(m_w) * size_t(m_h), 0);
        const int r = std::max(1, int(std::lround(keep_out / m_res)));
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r)
                    m_disc.emplace_back(dx, dy);
    }

    void stamp(const PlotPath &path)
    {
        each_sample(path, 0.7 * m_res, [this](const Vec2d &p) {
            const int cx = cell_x(p), cy = cell_y(p);
            for (const auto &[dx, dy] : m_disc) {
                const int x = cx + dx, y = cy + dy;
                if (x >= 0 && x < m_w && y >= 0 && y < m_h)
                    m_cells[size_t(y) * size_t(m_w) + size_t(x)] = 1;
            }
        });
    }

    // Fraction of the stroke's length lying on already-inked cells.
    double coverage(const PlotPath &path) const
    {
        size_t total = 0, hit = 0;
        each_sample(path, m_res, [&](const Vec2d &p) {
            ++total;
            const int x = cell_x(p), y = cell_y(p);
            if (x >= 0 && x < m_w && y >= 0 && y < m_h && m_cells[size_t(y) * size_t(m_w) + size_t(x)])
                ++hit;
        });
        return total == 0 ? 0. : double(hit) / double(total);
    }

private:
    int cell_x(const Vec2d &p) const { return int(std::floor((p.x() - m_min.x()) / m_res)); }
    int cell_y(const Vec2d &p) const { return int(std::floor((p.y() - m_min.y()) / m_res)); }

    template<class Fn> void each_sample(const PlotPath &path, double step, Fn fn) const
    {
        const auto  &pts = path.points;
        const size_t n   = pts.size();
        if (n == 0)
            return;
        fn(pts.front());
        const size_t last = (path.closed && n > 2) ? n : n - 1;
        for (size_t i = 0; i < last; ++i) {
            const Vec2d &a     = pts[i];
            const Vec2d &b     = pts[(i + 1) % n];
            const int    steps = std::max(1, int(std::ceil((b - a).norm() / step)));
            for (int k = 1; k <= steps; ++k)
                fn(a + (b - a) * (double(k) / steps));
        }
    }

    double               m_res = 0.1;
    Vec2d                m_min = Vec2d::Zero();
    int                  m_w = 0, m_h = 0;
    std::vector<uint8_t> m_cells;
    std::vector<std::pair<int, int>> m_disc;
};

// Centerlines for COMPLEX thin ink - crosshatch mats, stroke webs, or
// simply thousands of separate slivers. The exact Voronoi medial axis
// costs real time PER PIECE and minutes in aggregate on engraving-style
// art; one shared raster skeleton (Zhang-Suen thinning) at pen-scale
// resolution handles the whole set in linear time and renders the strands
// just as faithfully (the grid sits well below what the pen can resolve).
void raster_centerlines(const std::vector<const ExPolygon *> &pieces, double pen_width, double min_length, PlotPaths &out)
{
    BoundingBox bb;
    for (const ExPolygon *ex : pieces)
        bb.merge(get_extents(*ex));
    if (!bb.defined)
        return;
    double            res = std::min(std::max(pen_width / 4., 0.04), 0.15);
    const Vec2d       origin = unscale(bb.min) - Vec2d(2. * res, 2. * res);
    const Vec2d       span   = unscale(bb.max) - origin + Vec2d(2. * res, 2. * res);
    int W = std::max(4, int(std::ceil(span.x() / res)));
    int H = std::max(4, int(std::ceil(span.y() / res)));
    while (double(W) * double(H) > 64e6) {
        res *= 2.;
        W = (W + 1) / 2;
        H = (H + 1) / 2;
    }

    // Even-odd scanline fill of contour + holes.
    std::vector<uint8_t> px(size_t(W) * size_t(H), 0);
    struct Edge { double x1, y1, x2, y2; };
    std::vector<Edge> edges;
    auto add_ring = [&](const Polygon &ring) {
        const size_t n = ring.points.size();
        for (size_t i = 0; i < n; ++i) {
            const Vec2d a = unscale(ring.points[i]);
            const Vec2d b = unscale(ring.points[(i + 1) % n]);
            if (a.y() != b.y())
                edges.push_back({a.x(), a.y(), b.x(), b.y()});
        }
    };
    // Disjoint pieces keep even-odd parity intact in one shared edge set.
    for (const ExPolygon *ex : pieces) {
        add_ring(ex->contour);
        for (const Polygon &h : ex->holes)
            add_ring(h);
    }

    std::vector<std::vector<int>> rows(H);
    for (int i = 0; i < int(edges.size()); ++i) {
        const Edge &e  = edges[i];
        const double ylo = std::min(e.y1, e.y2), yhi = std::max(e.y1, e.y2);
        int jlo = std::max(0, int(std::floor((ylo - origin.y()) / res - 0.5)));
        int jhi = std::min(H - 1, int(std::ceil((yhi - origin.y()) / res)));
        for (int j = jlo; j <= jhi; ++j)
            rows[j].push_back(i);
    }
    std::vector<double> xs;
    for (int j = 0; j < H; ++j) {
        const double y = origin.y() + (j + 0.5) * res;
        xs.clear();
        for (int i : rows[j]) {
            const Edge &e = edges[i];
            // Half-open rule so shared vertices count once.
            if ((e.y1 <= y && e.y2 > y) || (e.y2 <= y && e.y1 > y))
                xs.push_back(e.x1 + (y - e.y1) * (e.x2 - e.x1) / (e.y2 - e.y1));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int clo = std::max(0, int(std::ceil((xs[k] - origin.x()) / res - 0.5)));
            int chi = std::min(W - 1, int(std::floor((xs[k + 1] - origin.x()) / res - 0.5)));
            for (int c = clo; c <= chi; ++c)
                px[size_t(j) * W + c] = 1;
        }
    }

    // Zhang-Suen thinning to a 1-px skeleton.
    auto at = [&](int r, int c) -> uint8_t & { return px[size_t(r) * W + c]; };
    std::vector<size_t> kill;
    for (bool changed = true; changed;) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            kill.clear();
            for (int r = 1; r < H - 1; ++r)
                for (int c = 1; c < W - 1; ++c) {
                    if (!at(r, c))
                        continue;
                    const uint8_t p2 = at(r - 1, c), p3 = at(r - 1, c + 1), p4 = at(r, c + 1), p5 = at(r + 1, c + 1),
                                  p6 = at(r + 1, c), p7 = at(r + 1, c - 1), p8 = at(r, c - 1), p9 = at(r - 1, c - 1);
                    const int bsum = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (bsum < 2 || bsum > 6)
                        continue;
                    const int a = (!p2 && p3) + (!p3 && p4) + (!p4 && p5) + (!p5 && p6) +
                                  (!p6 && p7) + (!p7 && p8) + (!p8 && p9) + (!p9 && p2);
                    if (a != 1)
                        continue;
                    if (pass == 0 ? (p2 * p4 * p6 || p4 * p6 * p8) : (p2 * p4 * p8 || p2 * p6 * p8))
                        continue;
                    kill.push_back(size_t(r) * W + c);
                }
            for (size_t idx : kill)
                px[idx] = 0;
            changed |= !kill.empty();
        }
    }

    // Walk the skeleton into chains; junctions split chains, which is fine
    // (PathOptimizer reorders, strands stay contiguous).
    static const int DR[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    static const int DC[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    auto degree = [&](int r, int c) {
        int d = 0;
        for (int k = 0; k < 8; ++k)
            if (at(r + DR[k], c + DC[k]))
                ++d;
        return d;
    };
    auto emit = [&](std::vector<Vec2d> &&chain) {
        simplify_dp(chain, 1.2 * res);
        append_plot_path(out, std::move(chain), false, min_length);
    };
    for (int endpoints_only = 1; endpoints_only >= 0; --endpoints_only) {
        for (int r = 1; r < H - 1; ++r)
            for (int c = 1; c < W - 1; ++c) {
                if (!at(r, c))
                    continue;
                if (endpoints_only && degree(r, c) == 2)
                    continue; // start walks at endpoints/junctions first
                std::vector<Vec2d> chain;
                int cr = r, cc = c;
                at(cr, cc) = 0;
                chain.emplace_back(origin + Vec2d((cc + 0.5) * res, (cr + 0.5) * res));
                for (bool moved = true; moved;) {
                    moved = false;
                    for (int k = 0; k < 8; ++k) {
                        const int nr = cr + DR[k], nc = cc + DC[k];
                        if (nr < 1 || nr >= H - 1 || nc < 1 || nc >= W - 1 || !at(nr, nc))
                            continue;
                        cr = nr; cc = nc;
                        at(cr, cc) = 0;
                        chain.emplace_back(origin + Vec2d((cc + 0.5) * res, (cr + 0.5) * res));
                        moved = true;
                        break;
                    }
                }
                if (chain.size() >= 2)
                    emit(std::move(chain));
            }
    }
}

void append_expolygon_contours(const ExPolygons &areas, double min_length, PlotPaths &out)
{
    for (const ExPolygon &ex : areas) {
        auto emit_ring = [&](const Polygon &ring) {
            std::vector<Vec2d> pts;
            pts.reserve(ring.points.size());
            for (const Point &pt : ring.points)
                pts.emplace_back(unscale(pt));
            append_plot_path(out, std::move(pts), true, min_length);
        };
        emit_ring(ex.contour);
        for (const Polygon &hole : ex.holes)
            emit_ring(hole);
    }
}

} // namespace

PlotPaths hatch_fill_regions(const std::vector<SvgFillRegion> &regions, const HatchParams &params)
{
    PlotPaths out;
    if (regions.empty() || params.spacing < 0.01)
        return out;

    const ExPolygons merged = compose_ink(regions);
    if (!merged.empty())
        hatch_areas(merged, params, out);
    return out;
}

PlotPaths plot_fill_regions(const std::vector<SvgFillRegion> &regions, const HatchParams &params)
{
    PlotPaths out;
    if (regions.empty() || params.spacing < 0.01)
        return out;

    const ExPolygons ink = compose_ink(regions);
    if (ink.empty())
        return out;

    // Split off ink a pen cannot outline-and-fill: parts thinner than
    // ~1.2x the tip. Morphological opening removes them; what remains is
    // the "thick" ink that gets outline + hatch.
    ExPolygons thick = ink;
    ExPolygons thin;
    if (params.centerline_thin) {
        const float half_limit = float(scale_(0.5 * 1.2 * std::max(params.pen_width, 0.05)));
        thick = offset2_ex(ink, -half_limit, half_limit);
        // Opening can poke past reflex corners; stay within the true ink.
        thick = intersection_ex(thick, ink);
        // Small clearance so slivers at thick/thin junctions don't double.
        thin = diff_ex(ink, offset_ex(thick, float(scale_(0.02))));
    }

    // Boundary outlines of the drawing (composed ink, not raw shapes).
    append_expolygon_contours(thick, params.min_length, out);

    // Interior coverage.
    hatch_areas(thick, params, out);

    // Thin parts: one stroke down the middle - the pen's own width renders
    // the artist's tapering lines instead of two overlapping outlines.
    // Split the thin ink between the exact medial axis (highest quality,
    // real cost per piece) and the shared raster skeleton (linear time).
    // Individual monsters (crosshatch webs full of holes) always go to the
    // raster; and when the ink is thousands of slivers whose exact-medial
    // calls would SUM to minutes, the whole set goes raster too.
    PlotPaths centers;
    std::vector<const ExPolygon *> webs;
    std::vector<const ExPolygon *> strokes;
    size_t stroke_pts = 0;
    for (const ExPolygon &ex : thin) {
        size_t npts = ex.contour.points.size();
        for (const Polygon &h : ex.holes)
            npts += h.points.size();
        if (npts > 2000 || ex.holes.size() > 30) {
            webs.push_back(&ex);
        } else {
            stroke_pts += npts;
            strokes.push_back(&ex);
        }
    }
    if (stroke_pts > 30000) {
        webs.insert(webs.end(), strokes.begin(), strokes.end());
        strokes.clear();
    }
    for (const ExPolygon *ex : strokes) {
        Polylines centerlines;
        ex->medial_axis(scale_(0.02), scale_(2.0 * 1.2 * std::max(params.pen_width, 0.05)), &centerlines);
        for (const Polyline &pl : centerlines) {
            std::vector<Vec2d> pts;
            pts.reserve(pl.points.size());
            for (const Point &pt : pl.points)
                pts.emplace_back(unscale(pt));
            append_plot_path(centers, std::move(pts), false, params.min_length);
        }
    }
    if (!webs.empty())
        raster_centerlines(webs, std::max(params.pen_width, 0.05), params.min_length, centers);

    if (params.density_limit && !centers.empty()) {
        BoundingBox bb;
        for (const ExPolygon &ex : ink)
            bb.merge(get_extents(ex));
        // Keep-out of 3/4 pen: a stroke closer than that to existing ink
        // overlaps it by more than a quarter of its width - fusion territory.
        const double pen = std::max(params.pen_width, 0.05);
        InkRaster raster(unscale(bb.min), unscale(bb.max), pen, 0.75 * pen);
        for (const PlotPath &p : out)
            raster.stamp(p);
        // Longest-first: long strokes carry the drawing's structure, short
        // ones its texture - when tone must go, texture goes first.
        std::vector<std::pair<double, size_t>> order;
        order.reserve(centers.size());
        for (size_t i = 0; i < centers.size(); ++i)
            order.emplace_back(centers[i].length(), i);
        std::sort(order.begin(), order.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        for (const auto &[len, idx] : order) {
            PlotPath &p = centers[idx];
            if (raster.coverage(p) > 0.70)
                continue;
            raster.stamp(p);
            out.emplace_back(std::move(p));
        }
    } else {
        for (PlotPath &p : centers)
            out.emplace_back(std::move(p));
    }
    return out;
}

} } // namespace Slic3r::Plotter
