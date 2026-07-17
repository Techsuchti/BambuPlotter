#include "PathOptimizer.hpp"

#include <limits>
#include <numeric>

namespace Slic3r { namespace Plotter {

// Greedy nearest-neighbor ordering over path endpoints. Open paths may be
// reversed; closed paths may be entered at any vertex (the point sequence is
// rotated so drawing starts at the vertex nearest to the current pen
// position). libslic3r's chain_polylines() is not used here: it indexes both
// endpoints of every segment in a KD tree, and a closed path represented
// with first == last collides in that structure (paths get duplicated or
// dropped), besides not supporting entry-vertex rotation.
PlotPaths PathOptimizer::optimize(PlotPaths paths, const Vec2d &start)
{
    PlotPaths in;
    in.reserve(paths.size());
    for (PlotPath &p : paths)
        if (!p.empty())
            in.emplace_back(std::move(p));

    PlotPaths out;
    out.reserve(in.size());
    std::vector<bool> used(in.size(), false);
    Vec2d pos = start;

    for (size_t round = 0; round < in.size(); ++round) {
        double best_cost    = std::numeric_limits<double>::max();
        size_t best_idx     = 0;
        bool   best_reverse = false;
        size_t best_entry   = 0; // closed paths: vertex index to start at

        for (size_t i = 0; i < in.size(); ++i) {
            if (used[i])
                continue;
            const PlotPath &p = in[i];
            if (p.closed) {
                for (size_t k = 0; k < p.points.size(); ++k) {
                    const double d = (p.points[k] - pos).squaredNorm();
                    if (d < best_cost) {
                        best_cost    = d;
                        best_idx     = i;
                        best_reverse = false;
                        best_entry   = k;
                    }
                }
            } else {
                const double d_fwd = (p.points.front() - pos).squaredNorm();
                const double d_rev = (p.points.back() - pos).squaredNorm();
                if (d_fwd < best_cost) {
                    best_cost = d_fwd; best_idx = i; best_reverse = false; best_entry = 0;
                }
                if (d_rev < best_cost) {
                    best_cost = d_rev; best_idx = i; best_reverse = true; best_entry = 0;
                }
            }
        }

        used[best_idx] = true;
        PlotPath p = std::move(in[best_idx]);
        if (p.closed) {
            if (best_entry != 0)
                std::rotate(p.points.begin(), p.points.begin() + best_entry, p.points.end());
            pos = p.points.front(); // a closed path exits where it entered
        } else {
            if (best_reverse)
                p.reverse();
            pos = p.points.back();
        }
        out.emplace_back(std::move(p));
    }
    return out;
}

} } // namespace Slic3r::Plotter
