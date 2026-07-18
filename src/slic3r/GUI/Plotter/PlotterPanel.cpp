#include "PlotterPanel.hpp"

#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/Plotter/PathOptimizer.hpp"
#include "libslic3r/Plotter/PlotterGCodeGenerator.hpp"
#include "libslic3r/Plotter/PlotterJobBuilder.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/StaticLine.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationController.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationDialog.hpp"
#include "slic3r/GUI/Plotter/PlotterPrintJob.hpp"

using namespace Slic3r::Plotter;

namespace Slic3r { namespace GUI {

namespace {

std::string projects_dir()
{
    return data_dir() + "/plotter/projects";
}

} // namespace

PlotterPanel::PlotterPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY)
{
    // No plater access here: the panel is constructed while the Plater itself
    // is still being built. Anything touching the plate waits for set_active.
    m_profile.load(PlotterCalibrationController::profile_path());
    SetBackgroundColour(*wxWHITE);
    build_ui();
    refresh_ui();
}

void PlotterPanel::build_ui()
{
    const int em  = wxGetApp().em_unit();
    const int gap = FromDIP(10);

    auto *root = new wxBoxSizer(wxVERTICAL);

    // Section title bars mirroring the native sidebar sections
    // ("Printer" / "Project Filaments" / "Process").
    auto add_title = [&](const wxString &text) {
        auto *bar = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxTAB_TRAVERSAL | wxBORDER_NONE);
        bar->SetBackgroundColor(wxColour(248, 248, 248));
        bar->SetBackgroundColor2(0xF1F1F1);
        auto *label = new Label(bar, text);
        auto *h     = new wxBoxSizer(wxHORIZONTAL);
        h->Add(label, 0, wxALIGN_CENTER | wxLEFT, em);
        h->SetMinSize(-1, 3 * em);
        bar->SetSizer(h);
        root->Add(bar, 0, wxEXPAND);
        auto *line = new ::StaticLine(this);
        line->SetLineColour("#CECECE");
        root->Add(line, 0, wxEXPAND);
    };

    // --- Calibration ------------------------------------------------------
    add_title(_L("Calibration"));
    m_profile_label = new wxStaticText(this, wxID_ANY, "");
    root->Add(m_profile_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    m_btn_calibrate = new Button(this, _L("Calibrate…"));
    m_btn_calibrate->Bind(wxEVT_BUTTON, &PlotterPanel::on_calibrate, this);
    root->Add(m_btn_calibrate, 0, wxLEFT | wxTOP | wxBOTTOM, gap);

    // --- Artwork ----------------------------------------------------------
    add_title(_L("Artwork"));
    m_btn_import = new Button(this, _L("Import SVG…"));
    m_btn_import->Bind(wxEVT_BUTTON, &PlotterPanel::on_import_svg, this);
    root->Add(m_btn_import, 0, wxLEFT | wxTOP, gap);
    m_source_label = new wxStaticText(this, wxID_ANY, _L("No artwork imported."));
    root->Add(m_source_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, gap);

    // --- Placement --------------------------------------------------------
    add_title(_L("Placement"));
    auto *pgrid = new wxFlexGridSizer(3, 2, FromDIP(6), FromDIP(8));
    auto add_spin = [&](const wxString &label, double init, double lo, double hi) {
        pgrid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto *s = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxSize(FromDIP(110), -1));
        s->SetRange(lo, hi);
        s->SetDigits(2);
        s->SetValue(init);
        s->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) { on_placement_changed(); });
        pgrid->Add(s, 0);
        return s;
    };
    m_scale_spin    = add_spin(_L("Scale"),         1.0, 0.01, 20.0);
    m_offset_x_spin = add_spin(_L("Offset X (mm)"), 0.0, -300.0, 300.0);
    m_offset_y_spin = add_spin(_L("Offset Y (mm)"), 0.0, -300.0, 300.0);
    root->Add(pgrid, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    m_btn_fit = new Button(this, _L("Fit to plotting area"));
    m_btn_fit->Bind(wxEVT_BUTTON, &PlotterPanel::on_fit_to_area, this);
    root->Add(m_btn_fit, 0, wxLEFT | wxTOP | wxBOTTOM, gap);

    // --- Status -----------------------------------------------------------
    // Actions live where they always did: project open/save in the File
    // menu, Generate plot / Send plot in the top-right MainFrame buttons.
    add_title(_L("Status"));
    m_stats_label = new wxStaticText(this, wxID_ANY, "");
    root->Add(m_stats_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    m_status_label = new wxStaticText(this, wxID_ANY, "");
    m_status_label->SetForegroundColour(ThemeColor::Danger);
    root->Add(m_status_label, 0, wxEXPAND | wxALL, gap);
    root->AddStretchSpacer();

    SetSizer(root);
}

void PlotterPanel::set_active(bool active)
{
    m_active = active;
    if (active) {
        // Re-read the profile: calibration may have run since construction.
        m_profile = Plotter::PlotterToolProfile();
        m_profile.load(PlotterCalibrationController::profile_path());
        refresh_ui();
    }
    update_plate_overlay();
}

MachineObject *PlotterPanel::selected_machine() const
{
    if (Slic3r::DeviceManager *dev = wxGetApp().getDeviceManager())
        return dev->get_selected_machine();
    return nullptr;
}

void PlotterPanel::update_plate_overlay()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    PartPlateList &plates = plater->get_partplate_list();

    const bool show_rect = m_active && m_profile.is_valid();
    plates.set_plot_rectangle(show_rect ? m_profile.pen_rect_on_bed() : BoundingBoxf(), show_rect);

    // Placed strokes as PEN positions on the bed (same transform as the
    // rectangle: machine coords + pen offset).
    std::vector<std::vector<Vec2d>> strokes;
    if (show_rect && m_has_project) {
        std::string err;
        PlotPaths placed = m_project.placed_paths(&err);
        const Vec2d to_bed = m_profile.paper_origin + m_profile.pen_offset;
        for (const PlotPath &path : placed) {
            std::vector<Vec2d> pts;
            pts.reserve(path.points.size() + 1);
            for (const Vec2d &pt : path.points)
                pts.emplace_back(pt + to_bed);
            if (path.closed && !pts.empty())
                pts.emplace_back(pts.front());
            if (pts.size() >= 2)
                strokes.emplace_back(std::move(pts));
        }
    }
    plates.set_plot_paths(strokes, !strokes.empty());

    plater->set_current_canvas_as_dirty();
}

void PlotterPanel::set_status(const std::string &msg)
{
    if (m_status_label) {
        m_status_label->SetLabelText(from_u8(msg));
        m_status_label->Wrap(GetClientSize().GetWidth() - FromDIP(16));
    }
    Layout();
}

void PlotterPanel::refresh_ui()
{
    const bool calibrated = m_profile.is_valid();
    if (m_profile_label) {
        if (calibrated) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "Calibrated: X %.1f–%.1f, Y %.1f–%.1f mm\npen Z %.2f/%.2f/%.2f",
                          m_profile.min_x, m_profile.max_x, m_profile.min_y, m_profile.max_y,
                          m_profile.pen_up_z, m_profile.pen_contact_z, m_profile.pen_down_z);
            m_profile_label->SetLabelText(buf);
        } else {
            m_profile_label->SetLabelText(_L("Not calibrated — run calibration first."));
        }
    }
    if (m_source_label && m_has_project) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s (%.0f × %.0f mm)",
                      boost::filesystem::path(m_project.source_svg_path).filename().string().c_str(),
                      m_project.doc_width, m_project.doc_height);
        m_source_label->SetLabelText(buf);
    }
    const bool ready = calibrated && m_has_project;
    if (m_btn_fit) m_btn_fit->Enable(ready);
    Layout();
}

void PlotterPanel::on_calibrate(wxCommandEvent &)
{
    PlotterCalibrationDialog dlg(wxGetApp().mainframe, selected_machine());
    if (dlg.ShowModal() == wxID_OK)
        m_profile = dlg.controller().profile();
    else {
        m_profile = Plotter::PlotterToolProfile();
        m_profile.load(PlotterCalibrationController::profile_path());
    }
    refresh_ui();
    update_plate_overlay();
}

void PlotterPanel::on_import_svg(wxCommandEvent &)
{
    wxFileDialog fd(this, _L("Import SVG"), "", "", "SVG files (*.svg)|*.svg",
                    wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (fd.ShowModal() != wxID_OK)
        return;
    import_svg_path(into_u8(fd.GetPath()));
}

bool PlotterPanel::import_svg_path(const std::string &path)
{
    std::string err;
    PlotterProject proj = PlotterProject::from_svg_file(path, SvgImportOptions{}, &err);
    if (!err.empty()) {
        set_status(err);
        return false;
    }
    m_project     = std::move(proj);
    m_has_project = true;
    m_project_path.clear(); // new, unsaved project
    set_status("");
    fit_to_area(); // sensible default placement
    refresh_ui();
    update_plate_overlay();
    return true;
}

void PlotterPanel::fit_to_area()
{
    if (!m_has_project || !m_profile.is_valid())
        return;
    const double doc_w = std::max(1e-3, m_project.doc_width);
    const double doc_h = std::max(1e-3, m_project.doc_height);
    const double margin = 2.0;
    const double avail_w = std::max(0., m_profile.plot_width() - 2 * margin);
    const double avail_h = std::max(0., m_profile.plot_height() - 2 * margin);
    const double scale = std::min(avail_w / doc_w, avail_h / doc_h);
    m_project.scale  = scale;
    // Center the scaled artwork inside the plotting rectangle (paper space,
    // origin at plotting-rect min corner).
    const double placed_w = doc_w * scale;
    const double placed_h = doc_h * scale;
    m_project.offset = Vec2d((m_profile.plot_width() - placed_w) / 2.,
                             (m_profile.plot_height() - placed_h) / 2.);
    if (m_scale_spin)    m_scale_spin->SetValue(m_project.scale);
    if (m_offset_x_spin) m_offset_x_spin->SetValue(m_project.offset.x());
    if (m_offset_y_spin) m_offset_y_spin->SetValue(m_project.offset.y());
}

void PlotterPanel::on_fit_to_area(wxCommandEvent &)
{
    fit_to_area();
    refresh_ui();
    update_plate_overlay();
}

void PlotterPanel::on_placement_changed()
{
    if (m_scale_spin)    m_project.scale    = m_scale_spin->GetValue();
    if (m_offset_x_spin) m_project.offset.x() = m_offset_x_spin->GetValue();
    if (m_offset_y_spin) m_project.offset.y() = m_offset_y_spin->GetValue();
    update_plate_overlay();
}

PlotPaths PlotterPanel::build_placed_paths(std::string *error) const
{
    PlotPaths placed = m_project.placed_paths(error);
    if (placed.empty())
        return placed;
    return PathOptimizer::optimize(std::move(placed), Vec2d(0., 0.));
}

int PlotterPanel::save_project_ui(bool save_as)
{
    if (!m_has_project) {
        set_status("nothing to save — import an SVG first");
        return wxID_CANCEL;
    }
    std::string path = m_project_path;
    if (save_as || path.empty()) {
        boost::system::error_code ec;
        boost::filesystem::create_directories(projects_dir(), ec);
        wxFileDialog fd(this, _L("Save plotter project"), projects_dir(), "plot.bplot",
                        "BambuPlotter project (*.bplot)|*.bplot", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (fd.ShowModal() != wxID_OK)
            return wxID_CANCEL;
        path = into_u8(fd.GetPath());
    }
    std::string err;
    if (!m_project.save(path, &err)) {
        set_status(err);
        return wxID_CANCEL;
    }
    m_project_path = path;
    set_status("");
    return wxID_YES;
}

bool PlotterPanel::open_project_ui(const wxString &filename)
{
    std::string path = into_u8(filename);
    if (path.empty()) {
        wxFileDialog fd(this, _L("Open plotter project or SVG"), projects_dir(), "",
                        "Plotter files (*.bplot;*.svg)|*.bplot;*.svg|"
                        "BambuPlotter project (*.bplot)|*.bplot|SVG files (*.svg)|*.svg",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (fd.ShowModal() != wxID_OK)
            return false;
        path = into_u8(fd.GetPath());
    }
    if (boost::algorithm::iends_with(path, ".svg"))
        return import_svg_path(path);

    std::string err;
    PlotterProject proj;
    if (!proj.load(path, &err)) {
        set_status(err);
        return false;
    }
    m_project      = std::move(proj);
    m_has_project  = true;
    m_project_path = path;
    if (m_scale_spin)    m_scale_spin->SetValue(m_project.scale);
    if (m_offset_x_spin) m_offset_x_spin->SetValue(m_project.offset.x());
    if (m_offset_y_spin) m_offset_y_spin->SetValue(m_project.offset.y());
    set_status("");
    refresh_ui();
    update_plate_overlay();
    return true;
}

void PlotterPanel::generate_plot()
{
    if (!m_has_project) {
        set_status("Import an SVG first.");
        return;
    }
    if (!m_profile.is_valid()) {
        set_status("Calibration required before plotting.");
        return;
    }
    std::string err;
    const PlotPaths paths = build_placed_paths(&err);
    if (paths.empty()) {
        set_status(err.empty() ? "nothing to plot" : err);
        return;
    }
    const GCodeGenResult gen = PlotterGCodeGenerator::generate(paths, m_profile);
    if (!gen.ok) {
        set_status(gen.error);
        return;
    }
    // Write the raw movement G-code and open it in the existing G-code viewer.
    const std::string preview_path = data_dir() + "/plotter/preview.gcode";
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(boost::filesystem::path(preview_path).parent_path(), ec);
        std::ofstream out(preview_path, std::ios::trunc);
        out << gen.gcode;
    }
    char stats[160];
    std::snprintf(stats, sizeof(stats), "%zu strokes, draw %.0f mm, travel %.0f mm",
                  gen.path_count, gen.draw_length, gen.travel_length);
    m_stats_label->SetLabelText(stats);
    set_status("");
    if (auto *plater = wxGetApp().plater()) {
        plater->load_gcode(from_u8(preview_path));
        // Mirror the native slice flow: show the result in the Preview tab.
        if (auto *mainframe = wxGetApp().mainframe)
            mainframe->select_tab(size_t(MainFrame::tpPreview));
    }
}

void PlotterPanel::send_plot()
{
    if (!m_has_project) {
        set_status("Import an SVG first.");
        return;
    }
    if (!m_profile.is_valid()) {
        set_status("Calibration required before plotting.");
        return;
    }
    MachineObject *machine = selected_machine();
    if (machine == nullptr) {
        set_status("no printer connected — select one in the Device tab");
        return;
    }
    std::string err;
    const PlotPaths paths = build_placed_paths(&err);
    if (paths.empty()) {
        set_status(err.empty() ? "nothing to plot" : err);
        return;
    }
    const std::string name = boost::filesystem::path(m_project.source_svg_path).stem().string();
    const PlotterJob job = PlotterJobBuilder::build(paths, m_profile, name.empty() ? "plot" : name,
                                                    resources_dir() + "/plotter");
    if (!job.ok) {
        set_status(job.error);
        return;
    }
    set_status("");
    const PlotterPrintResult r = PlotterPrintJob::upload_and_start(
        machine, job, [this](const std::string &stage, int pct) {
            m_stats_label->SetLabelText(from_u8(stage) + wxString::Format(" (%d%%)", pct));
            wxYield();
        });
    if (!r.ok)
        set_status(r.error);
    else
        set_status("Job sent — the printer will run it. You can disconnect now.");
}

} } // namespace Slic3r::GUI
