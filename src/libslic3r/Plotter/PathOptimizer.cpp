#include "PathOptimizer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r { namespace Plotter {

// Greedy nearest-neighbor ordering over path endpoints. Open paths may be
// reversed; closed paths may be entered at (nearly) any vertex - the point
// sequence is rotated so drawing starts close to the current pen position.
// libslic3r's chain_polylines() is not used here: it indexes both endpoints
// of every segment in a KD tree, and a closed path represented with
// first == last collides in that structure (paths get duplicated or
// dropped), besides not supporting entry-vertex rotation.
//
// The nearest query runs against a uniform hash grid of candidate entry
// points, walked outward ring by ring. The naive scan (every vertex of
// every remaining path, every round) was O(rounds x total vertices) and
// took MINUTES on engraving-class plans (~9k paths); the grid makes the
// ordering effectively linear. Closed paths contribute at most ~24 sampled
// entry vertices - entry granularity of a few millimeters is invisible
// next to the pen-up travel it optimizes (small rings keep every vertex).
PlotPaths PathOptimizer::optimize(PlotPaths paths, const Vec2d &start)
{
    PlotPaths in;
    in.reserve(paths.size());
    for (PlotPath &p : paths)
        if (!p.empty())
            in.emplace_back(std::move(p));
    if (in.empty())
        return in;

    struct Candidate
    {
        uint32_t path;
        uint32_t entry;   // vertex index (closed paths)
        bool     reverse; // open paths: enter at the back
        Vec2d    pt;
        int64_t  cell;
    };

    constexpr double CELL = 5.0; // mm
    auto cell_xy = [&](const Vec2d &p) -> std::pair<int64_t, int64_t> {
        return {int64_t(std::floor(p.x() / CELL)), int64_t(std::floor(p.y() / CELL))};
    };
    auto cell_key = [](int64_t cx, int64_t cy) -> int64_t { return (cx << 32) ^ (cy & 0xffffffffll); };

    std::vector<Candidate>                             cands;
    std::vector<std::vector<uint32_t>>                 path_cands(in.size());
    std::unordered_map<int64_t, std::vector<uint32_t>> grid;
    auto add_candidate = [&](uint32_t path, uint32_t entry, bool reverse, const Vec2d &pt) {
        const auto [cx, cy] = cell_xy(pt);
        const int64_t  key  = cell_key(cx, cy);
        const uint32_t idx  = uint32_t(cands.size());
        cands.push_back({path, entry, reverse, pt, key});
        path_cands[path].push_back(idx);
        grid[key].push_back(idx);
    };
    for (uint32_t i = 0; i < uint32_t(in.size()); ++i) {
        const PlotPath &p = in[i];
        if (p.closed) {
            const size_t step = std::max<size_t>(1, p.points.size() / 24);
            for (size_t k = 0; k < p.points.size(); k += step)
                add_candidate(i, uint32_t(k), false, p.points[k]);
        } else {
            add_candidate(i, 0, false, p.points.front());
            add_candidate(i, 0, true, p.points.back());
        }
    }

    std::vector<bool> used(in.size(), false);
    PlotPaths out;
    out.reserve(in.size());
    Vec2d pos = start;

    for (size_t round = 0; round < in.size(); ++round) {
        // Expanding ring search. A candidate in Chebyshev ring R is at
        // least (R-1)*CELL away from pos, so once the best find beats
        // R*CELL nothing farther out can win.
        double           best_d2 = std::numeric_limits<double>::max();
        const Candidate *best    = nullptr;
        const auto [cx0, cy0]    = cell_xy(pos);
        for (int64_t r = 0; best == nullptr || best_d2 > (double(r - 1) * CELL) * (double(r - 1) * CELL); ++r) {
            for (int64_t dx = -r; dx <= r; ++dx)
                for (int64_t dy = -r; dy <= r; ++dy) {
                    if (std::max(std::llabs(dx), std::llabs(dy)) != r)
                        continue;
                    auto it = grid.find(cell_key(cx0 + dx, cy0 + dy));
                    if (it == grid.end())
                        continue;
                    for (uint32_t idx : it->second) {
                        const Candidate &c = cands[idx];
                        if (used[c.path])
                            continue;
                        const double d2 = (c.pt - pos).squaredNorm();
                        if (d2 < best_d2) {
                            best_d2 = d2;
                            best    = &c;
                        }
                    }
                }
            // The paper is finite; way past any plausible bed there is
            // nothing left to find.
            if (best == nullptr && r > 4000)
                break;
        }
        if (best == nullptr)
            break; // defensive: no alive candidate reachable

        used[best->path] = true;
        // Retire the chosen path's candidates so late rounds stay fast.
        for (uint32_t idx : path_cands[best->path]) {
            auto cell_it = grid.find(cands[idx].cell);
            if (cell_it == grid.end())
                continue;
            auto &cell = cell_it->second;
            for (size_t k = 0; k < cell.size(); ++k)
                if (cell[k] == idx) {
                    cell[k] = cell.back();
                    cell.pop_back();
                    break;
                }
            if (cell.empty())
                grid.erase(cell_it);
        }

        PlotPath p = std::move(in[best->path]);
        if (p.closed) {
            if (best->entry != 0)
                std::rotate(p.points.begin(), p.points.begin() + best->entry, p.points.end());
            pos = p.points.front(); // a closed path exits where it entered
        } else {
            if (best->reverse)
                p.reverse();
            pos = p.points.back();
        }
        out.emplace_back(std::move(p));
    }
    return out;
}

} } // namespace Slic3r::Plotter
