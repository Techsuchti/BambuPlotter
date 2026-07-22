#ifndef slic3r_ArtworkMesh_hpp_
#define slic3r_ArtworkMesh_hpp_

#include "PlotterPath.hpp"

struct indexed_triangle_set;

namespace Slic3r { namespace Plotter {

// Display mesh for SVG artwork on the plate: every stroke segment becomes a
// thin closed box (a triangle soup of disconnected shells — 0 open edges,
// 0 non-manifold edges, so the object list shows no repair warning).
//
// The mesh exists only so the artwork is a real, pickable, gizmo-driven
// plate object; G-code is always generated from the paths, never from the
// mesh.
//
// XY vertices are emitted as (p - pivot), so the mesh origin is the pivot
// and the owning ModelVolume keeps an identity transform.
struct ArtworkMeshParams
{
    double stroke_width  = 0.5; // mm, pen tip diameter
    double stroke_height = 0.2; // mm, visual ink thickness
    // Above this many segments the paths are decimated (Douglas-Peucker with
    // doubling tolerance) before meshing; display-only, paths are unaffected.
    size_t max_segments  = 20000;
};

indexed_triangle_set artwork_mesh(const PlotPaths &paths,
                                  const Vec2d     &pivot,
                                  const ArtworkMeshParams &params = {});

} } // namespace Slic3r::Plotter

#endif // slic3r_ArtworkMesh_hpp_
