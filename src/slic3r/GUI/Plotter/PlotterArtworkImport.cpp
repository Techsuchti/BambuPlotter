#include "PlotterArtworkImport.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <boost/filesystem/path.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Plotter/ArtworkMesh.hpp"
#include "libslic3r/Plotter/FillHatcher.hpp"
#include "libslic3r/Plotter/PlotterProject.hpp"
#include "libslic3r/Plotter/PlotterToolProfile.hpp"
#include "libslic3r/Plotter/RasterPlotImporter.hpp"
#include "libslic3r/Plotter/SvgPlotImporter.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationController.hpp"
#include "slic3r/GUI/Plotter/PlotterController.hpp"

namespace Slic3r { namespace GUI {

using namespace Slic3r::Plotter;

namespace {

bool model_has_artwork(const Model &model)
{
    for (const ModelObject *obj : model.objects)
        for (const ModelVolume *vol : obj->volumes)
            if (vol->is_plotter_artwork())
                return true;
    return false;
}

void show_import_error(const wxString &message)
{
    MessageDialog dlg(wxGetApp().mainframe, message, _L("Import artwork"), wxOK | wxICON_WARNING);
    dlg.ShowModal();
}

// Common tail of every artwork import: the info's embedded markup becomes a
// plate object (display mesh, paper fit, placement, snapshot).
bool finish_artwork_import(Plater &plater, ArtworkInfo &&info, const std::string &source_path)
{
    const SvgImportResult imported = SvgPlotImporter::import_memory(info.svg_markup, info.options);
    if (!imported.ok) {
        show_import_error(_L("No plottable strokes found in the artwork:") + " " + from_u8(imported.error));
        return false;
    }
    info.doc_width  = imported.width;
    info.doc_height = imported.height;

    // Display geometry: stroke paths plus the contours of filled shapes.
    PlotPaths display = imported.paths;
    for (const SvgFillRegion &region : imported.fill_regions)
        display.insert(display.end(), region.contours.begin(), region.contours.end());
    if (display.empty()) {
        show_import_error(_L("No plottable strokes found in the artwork:") + " " + _L("empty document"));
        return false;
    }

    const BoundingBoxf ink = get_extents(display);
    info.pivot = ink.center();

    // Paper rectangle in bed coordinates; before calibration fall back to
    // the build plate so importing still works.
    PlotterToolProfile profile;
    profile.load(PlotterCalibrationController::profile_path(), nullptr);

    ArtworkMeshParams mesh_params;
    if (profile.pen_tip_width > 0.05)
        mesh_params.stroke_width = profile.pen_tip_width;
    const TriangleMesh mesh(artwork_mesh(display, info.pivot, mesh_params));
    if (mesh.empty()) {
        show_import_error(_L("The artwork produced no drawable geometry."));
        return false;
    }
    const BoundingBoxf paper = profile.is_valid() ?
        profile.pen_rect_on_bed() :
        plater.build_volume().bounding_volume2d();

    // Fit into the paper with a 2 mm margin (never scale up on import).
    const Vec2d ink_size = ink.size();
    double s0 = 1.;
    if (ink_size.x() > 1e-6 && ink_size.y() > 1e-6)
        s0 = std::min({1.,
                       (paper.size().x() - 2.) / ink_size.x(),
                       (paper.size().y() - 2.) / ink_size.y()});
    s0 = std::max(s0, 0.01);

    Vec2d position = paper.center();
    if (model_has_artwork(plater.model())) {
        // Find a free spot for additional artworks, clamped onto the paper.
        const Vec2d scaled_half = ink_size * s0 * 0.5;
        auto cell = plater.canvas3D()->get_nearest_empty_cell(
            {position.x(), position.y()},
            {ink_size.x() * s0 + 1., ink_size.y() * s0 + 1.});
        position = Vec2d(cell(0), cell(1));
        position.x() = std::clamp(position.x(), paper.min.x() + scaled_half.x(), paper.max.x() - scaled_half.x());
        position.y() = std::clamp(position.y(), paper.min.y() + scaled_half.y(), paper.max.y() - scaled_half.y());
    }

    const wxString name = from_u8(boost::filesystem::path(source_path).stem().string());

    Plater::TakeSnapshot snapshot(&plater, "Import artwork");
    wxGetApp().obj_list()->load_artwork_object(mesh, name, info, position, s0);
    return true;
}

} // namespace

bool import_svg_as_artwork(Plater &plater, const std::string &svg_path)
{
    // Embed the markup so the .bplot project stays portable.
    std::ifstream in(svg_path, std::ios::binary);
    if (!in) {
        show_import_error(_L("Cannot open SVG file:") + " " + from_u8(svg_path));
        return false;
    }
    std::stringstream markup;
    markup << in.rdbuf();

    ArtworkInfo info;
    info.source_svg_path = svg_path;
    info.svg_markup      = markup.str();
    return finish_artwork_import(plater, std::move(info), svg_path);
}

bool import_png_as_artwork(Plater &plater, const std::string &png_path)
{
    // Trace the bitmap's ink into an even-odd SVG document; from here on a
    // PNG-born artwork is indistinguishable from an imported SVG (.bplot
    // embeds the traced markup, the fill pipeline applies unchanged).
    const RasterTraceResult traced = trace_png_to_svg(png_path);
    if (!traced.ok) {
        show_import_error(_L("Could not trace the image into pen artwork:") + " " + from_u8(traced.error));
        return false;
    }

    ArtworkInfo info;
    info.source_svg_path = png_path;
    info.svg_markup      = traced.svg_markup;
    return finish_artwork_import(plater, std::move(info), png_path);
}

std::vector<ArtworkSnapshot> collect_artwork_snapshots(const Model &model, std::string *error)
{
    std::vector<ArtworkSnapshot> snapshots;
    for (const ModelObject *obj : model.objects)
        for (const ModelVolume *vol : obj->volumes) {
            if (!vol->is_plotter_artwork())
                continue;
            for (const ModelInstance *inst : obj->instances) {
                ArtworkSnapshot snap;
                snap.info  = *vol->plotter_artwork;
                snap.world = inst->get_transformation().get_matrix() *
                             vol->get_transformation().get_matrix();
                snap.name  = obj->name;
                snapshots.emplace_back(std::move(snap));
            }
        }
    if (snapshots.empty() && error != nullptr)
        *error = "there is no artwork on the plate - import an SVG first";
    return snapshots;
}

PlotPaths paper_paths_from_snapshots(const std::vector<ArtworkSnapshot> &snapshots,
                                     const PlotterToolProfile &profile,
                                     std::string *error)
{
    PlotPaths out;
    // Pen-tip bed position of the paper origin (see PlotterToolProfile.hpp:
    // paper_origin is commanded XY, the ink lands pen_offset away from it).
    const Vec2d anchor = profile.paper_origin + profile.pen_offset;

    for (const ArtworkSnapshot &snap : snapshots) {
        std::string import_error;
        SvgImportResult imported = snap.info.import_result(&import_error);
        PlotPaths doc = std::move(imported.paths); // stroke-painted shapes
        if (!imported.fill_regions.empty()) {
            if (profile.fill_enabled) {
                // Full fill plan: composed-ink outlines + hatch +
                // centerlines for thin parts.
                HatchParams hatch_params;
                hatch_params.pattern   = profile.hatch_pattern == 1 ? HatchPattern::Concentric : HatchPattern::Lines;
                hatch_params.spacing   = profile.hatch_spacing();
                hatch_params.angle_deg = profile.hatch_angle;
                hatch_params.inset     = 0.5 * profile.pen_tip_width;
                hatch_params.pen_width = profile.pen_tip_width;
                PlotPaths fill = plot_fill_regions(imported.fill_regions, hatch_params);
                doc.insert(doc.end(), std::make_move_iterator(fill.begin()),
                           std::make_move_iterator(fill.end()));
            } else {
                // Fill disabled: draw the raw contours as line art.
                for (const SvgFillRegion &region : imported.fill_regions)
                    doc.insert(doc.end(), region.contours.begin(), region.contours.end());
            }
        }
        if (doc.empty()) {
            if (error != nullptr)
                *error = snap.name + ": " +
                         (import_error.empty() ? std::string("artwork has no strokes") : import_error);
            return {};
        }

        // The pen only ever draws in the plate plane.
        const Vec3d uz = snap.world.linear() * Vec3d(0., 0., 1.);
        if (uz.norm() < 1e-9 || std::abs(uz.z()) / uz.norm() < 0.999) {
            if (error != nullptr)
                *error = snap.name + ": artwork is rotated off the plate plane; reset its rotation";
            return {};
        }
        const Vec2d pivot = snap.info.pivot;
        for (const PlotPath &p : doc) {
            PlotPath t;
            t.closed = p.closed;
            t.points.reserve(p.points.size());
            for (const Vec2d &pt : p.points) {
                const Vec3d bed = snap.world * Vec3d(pt.x() - pivot.x(), pt.y() - pivot.y(), 0.);
                t.points.emplace_back(bed.x() - anchor.x(), bed.y() - anchor.y());
            }
            out.emplace_back(std::move(t));
        }
    }

    if (out.empty() && error != nullptr)
        *error = "there is no artwork on the plate - import an SVG first";
    return out;
}

PlotPaths collect_artwork_paper_paths(const Model &model,
                                      const PlotterToolProfile &profile,
                                      std::string *error)
{
    const std::vector<ArtworkSnapshot> snapshots = collect_artwork_snapshots(model, error);
    if (snapshots.empty())
        return {};
    return paper_paths_from_snapshots(snapshots, profile, error);
}

bool save_plotter_project(Plater &plater, const std::string &path, std::string *error)
{
    PlotterProject project;
    for (const ModelObject *obj : plater.model().objects) {
        const ModelVolume *artwork = nullptr;
        for (const ModelVolume *vol : obj->volumes)
            if (vol->is_plotter_artwork()) { artwork = vol; break; }
        if (artwork == nullptr)
            continue;
        for (const ModelInstance *inst : obj->instances) {
            ProjectArtwork a;
            a.name                 = obj->name;
            a.info                 = *artwork->plotter_artwork;
            a.placement.offset     = Vec2d(inst->get_offset().x(), inst->get_offset().y());
            a.placement.rotation_z = inst->get_rotation().z();
            a.placement.scale      = inst->get_scaling_factor().x();
            project.artworks.emplace_back(std::move(a));
        }
    }
    if (project.artworks.empty()) {
        if (error != nullptr)
            *error = "there is no artwork on the plate - nothing to save";
        return false;
    }

    // Plot intent travels with the project (calibration never does).
    const PlotterToolProfile &profile = plater.plotter_controller()->profile();
    project.has_settings                  = true;
    project.settings.pen_tip_width        = profile.pen_tip_width;
    project.settings.travel_speed         = profile.travel_speed;
    project.settings.draw_speed           = profile.draw_speed;
    project.settings.lift_speed           = profile.lift_speed;
    project.settings.fill_enabled         = profile.fill_enabled;
    project.settings.hatch_pattern        = profile.hatch_pattern;
    project.settings.hatch_spacing_factor = profile.hatch_spacing_factor;
    project.settings.hatch_angle          = profile.hatch_angle;

    return project.save(path, error);
}

bool open_plotter_project(Plater &plater, const std::string &path)
{
    PlotterProject project;
    std::string error;
    if (!project.load(path, &error)) {
        show_import_error(_L("Cannot open the plot project:") + " " + from_u8(error));
        return false;
    }
    if (project.artworks.empty()) {
        show_import_error(_L("The plot project contains no artwork."));
        return false;
    }

    PlotterToolProfile profile;
    profile.load(PlotterCalibrationController::profile_path(), nullptr);
    ArtworkMeshParams mesh_params;
    if (profile.pen_tip_width > 0.05)
        mesh_params.stroke_width = profile.pen_tip_width;

    Plater::TakeSnapshot snapshot(&plater, "Open plot project");

    // The project replaces the current artwork.
    Model &model = plater.model();
    for (int i = int(model.objects.size()) - 1; i >= 0; --i) {
        const ModelObject *obj = model.objects[size_t(i)];
        const bool is_artwork = std::any_of(obj->volumes.begin(), obj->volumes.end(),
            [](const ModelVolume *v) { return v->is_plotter_artwork(); });
        if (is_artwork)
            plater.remove(size_t(i));
    }

    size_t restored = 0;
    for (const ProjectArtwork &a : project.artworks) {
        std::string import_error;
        SvgImportResult imported = a.info.import_result(&import_error);
        PlotPaths doc = std::move(imported.paths);
        for (const SvgFillRegion &region : imported.fill_regions)
            doc.insert(doc.end(), region.contours.begin(), region.contours.end());
        if (doc.empty()) {
            show_import_error(from_u8(a.name) + ": " +
                              _L("could not restore this artwork:") + " " + from_u8(import_error));
            continue;
        }
        const TriangleMesh mesh(artwork_mesh(doc, a.info.pivot, mesh_params));
        if (mesh.empty())
            continue;
        const wxString name = a.name.empty() ? wxString(_L("artwork")) : from_u8(a.name);
        wxGetApp().obj_list()->load_artwork_object(mesh, name, a.info,
                                                   a.placement.offset, a.placement.scale,
                                                   a.placement.rotation_z);
        ++restored;
    }
    if (restored == 0) {
        show_import_error(_L("No artwork could be restored from the plot project."));
        return false;
    }

    // Apply the project's plot intent to the machine profile - pen/fill
    // fields only, never the calibration.
    if (project.has_settings) {
        PlotterToolProfile current;
        current.load(PlotterCalibrationController::profile_path(), nullptr);
        current.pen_tip_width        = project.settings.pen_tip_width;
        current.travel_speed         = project.settings.travel_speed;
        current.draw_speed           = project.settings.draw_speed;
        current.lift_speed           = project.settings.lift_speed;
        current.fill_enabled         = project.settings.fill_enabled;
        current.hatch_pattern        = project.settings.hatch_pattern;
        current.hatch_spacing_factor = project.settings.hatch_spacing_factor;
        current.hatch_angle          = project.settings.hatch_angle;
        std::string save_error;
        current.save(PlotterCalibrationController::profile_path(), &save_error);
        plater.plotter_controller()->reload_profile();
        plater.sidebar().refresh_plotter_config();
    }

    plater.plotter_controller()->set_project_path(path);
    plater.set_project_filename(from_u8(path));
    return true;
}

} } // namespace Slic3r::GUI
