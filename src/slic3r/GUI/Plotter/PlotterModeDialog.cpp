#include "PlotterModeDialog.hpp"

#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <boost/filesystem.hpp>

#include "libslic3r/Plotter/PathOptimizer.hpp"
#include "libslic3r/Plotter/PlotterGCodeGenerator.hpp"
#include "libslic3r/Plotter/PlotterJobBuilder.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationController.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationDialog.hpp"
#include "slic3r/GUI/Plotter/PlotterPrintJob.hpp"

using namespace Slic3r::Plotter;

namespace Slic3r { namespace GUI {

namespace {

void style_primary_button(Button *btn)
{
    StateColor bg(std::pair<wxColour, int>(wxColour(27, 96, 136), StateColor::Pressed),
                  std::pair<wxColour, int>(wxColour(90, 120, 145), StateColor::Hovered),
                  std::pair<wxColour, int>(wxColour(110, 140, 160), StateColor::Normal));
    btn->SetBackgroundColor(bg);
    btn->SetBorderColor(StateColor(std::pair<wxColour, int>(wxColour(110, 140, 160), StateColor::Normal)));
    btn->SetTextColor(StateColor(std::pair<wxColour, int>(*wxWHITE, StateColor::Normal)));
}

wxStaticText *section(wxWindow *p, const wxString &t)
{
    auto *s = new wxStaticText(p, wxID_ANY, t);
    s->SetFont(Label::Head_14);
    return s;
}

std::string projects_dir()
{
    return data_dir() + "/plotter/projects";
}

} // namespace

PlotterModeDialog::PlotterModeDialog(wxWindow *parent, MachineObject *machine)
    : DPIDialog(parent, wxID_ANY, _L("Plotter Mode"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_machine(machine)
{
    m_profile.load(PlotterCalibrationController::profile_path());
    SetBackgroundColour(*wxWHITE);
    build_ui();
    refresh_ui();
}

void PlotterModeDialog::build_ui()
{
    auto *root = new wxBoxSizer(wxVERTICAL);
    const int gap = FromDIP(8);

    // --- calibration status --------------------------------------------
    root->Add(section(this, _L("1. Calibration")), 0, wxLEFT | wxTOP, gap);
    m_profile_label = new wxStaticText(this, wxID_ANY, "");
    root->Add(m_profile_label, 0, wxLEFT | wxRIGHT, gap);
    m_btn_calibrate = new Button(this, _L("Calibrate…"));
    style_primary_button(m_btn_calibrate);
    m_btn_calibrate->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_calibrate, this);
    root->Add(m_btn_calibrate, 0, wxALL, gap);

    // --- SVG import -----------------------------------------------------
    root->Add(section(this, _L("2. Artwork")), 0, wxLEFT | wxTOP, gap);
    m_btn_import = new Button(this, _L("Import SVG…"));
    style_primary_button(m_btn_import);
    m_btn_import->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_import_svg, this);
    root->Add(m_btn_import, 0, wxALL, gap);
    m_source_label = new wxStaticText(this, wxID_ANY, _L("No artwork imported."));
    root->Add(m_source_label, 0, wxLEFT | wxRIGHT, gap);

    // --- placement ------------------------------------------------------
    root->Add(section(this, _L("3. Placement (mm)")), 0, wxLEFT | wxTOP, gap);
    auto *pgrid = new wxFlexGridSizer(3, 2, FromDIP(4), FromDIP(8));
    auto add_spin = [&](const wxString &label, double init, double lo, double hi) {
        pgrid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto *s = new wxSpinCtrlDouble(this, wxID_ANY);
        s->SetRange(lo, hi);
        s->SetDigits(2);
        s->SetValue(init);
        s->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) { on_placement_changed(); });
        pgrid->Add(s, 0);
        return s;
    };
    m_scale_spin    = add_spin(_L("Scale"),    1.0, 0.01, 20.0);
    m_offset_x_spin = add_spin(_L("Offset X"), 0.0, -300.0, 300.0);
    m_offset_y_spin = add_spin(_L("Offset Y"), 0.0, -300.0, 300.0);
    root->Add(pgrid, 0, wxALL, gap);
    m_btn_fit = new Button(this, _L("Fit to plotting area"));
    style_primary_button(m_btn_fit);
    m_btn_fit->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_fit_to_area, this);
    root->Add(m_btn_fit, 0, wxALL, gap);

    // --- project + output ----------------------------------------------
    root->Add(section(this, _L("4. Project & output")), 0, wxLEFT | wxTOP, gap);
    auto *brow = new wxBoxSizer(wxHORIZONTAL);
    m_btn_open    = new Button(this, _L("Open project…"));
    m_btn_save    = new Button(this, _L("Save project…"));
    m_btn_preview = new Button(this, _L("Preview G-code"));
    m_btn_send    = new Button(this, _L("Send to printer"));
    for (Button *b : {m_btn_open, m_btn_save, m_btn_preview, m_btn_send}) {
        style_primary_button(b);
        brow->Add(b, 0, wxRIGHT, gap);
    }
    m_btn_open->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_open_project, this);
    m_btn_save->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_save_project, this);
    m_btn_preview->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_generate_preview, this);
    m_btn_send->Bind(wxEVT_BUTTON, &PlotterModeDialog::on_send, this);
    root->Add(brow, 0, wxALL, gap);

    m_stats_label = new wxStaticText(this, wxID_ANY, "");
    root->Add(m_stats_label, 0, wxLEFT | wxRIGHT, gap);
    m_status_label = new wxStaticText(this, wxID_ANY, "");
    m_status_label->SetForegroundColour(wxColour(200, 60, 60));
    root->Add(m_status_label, 0, wxALL, gap);

    SetSizerAndFit(root);
}

void PlotterModeDialog::set_status(const std::string &msg)
{
    if (m_status_label) m_status_label->SetLabelText(from_u8(msg));
    Layout();
}

void PlotterModeDialog::refresh_ui()
{
    const bool calibrated = m_profile.is_valid();
    if (m_profile_label) {
        if (calibrated) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "Calibrated: X %.1f–%.1f, Y %.1f–%.1f mm, pen Z %.2f/%.2f/%.2f",
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
    if (m_btn_preview) m_btn_preview->Enable(ready);
    if (m_btn_send)    m_btn_send->Enable(ready && m_machine != nullptr);
    if (m_btn_save)    m_btn_save->Enable(m_has_project);
    if (m_btn_fit)     m_btn_fit->Enable(ready);
    Layout();
}

bool PlotterModeDialog::ensure_profile()
{
    if (m_profile.is_valid())
        return true;
    set_status("Calibration required before plotting.");
    return false;
}

void PlotterModeDialog::on_calibrate(wxCommandEvent &)
{
    PlotterCalibrationDialog dlg(this, m_machine);
    if (dlg.ShowModal() == wxID_OK)
        m_profile = dlg.controller().profile();
    else
        m_profile.load(PlotterCalibrationController::profile_path());
    refresh_ui();
}

void PlotterModeDialog::on_import_svg(wxCommandEvent &)
{
    wxFileDialog fd(this, _L("Import SVG"), "", "", "SVG files (*.svg)|*.svg",
                    wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (fd.ShowModal() != wxID_OK)
        return;
    std::string err;
    PlotterProject proj = PlotterProject::from_svg_file(into_u8(fd.GetPath()), SvgImportOptions{}, &err);
    if (!err.empty()) {
        set_status(err);
        return;
    }
    m_project     = std::move(proj);
    m_has_project = true;
    set_status("");
    on_fit_to_area(*(new wxCommandEvent()));  // sensible default placement
    refresh_ui();
}

void PlotterModeDialog::on_fit_to_area(wxCommandEvent &)
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
    refresh_ui();
}

void PlotterModeDialog::on_placement_changed()
{
    if (m_scale_spin)    m_project.scale    = m_scale_spin->GetValue();
    if (m_offset_x_spin) m_project.offset.x() = m_offset_x_spin->GetValue();
    if (m_offset_y_spin) m_project.offset.y() = m_offset_y_spin->GetValue();
}

PlotPaths PlotterModeDialog::build_placed_paths(std::string *error) const
{
    PlotPaths placed = m_project.placed_paths(error);
    if (placed.empty())
        return placed;
    return PathOptimizer::optimize(std::move(placed), Vec2d(0., 0.));
}

void PlotterModeDialog::on_save_project(wxCommandEvent &)
{
    if (!m_has_project)
        return;
    boost::system::error_code ec;
    boost::filesystem::create_directories(projects_dir(), ec);
    wxFileDialog fd(this, _L("Save plotter project"), projects_dir(), "plot.bplot",
                    "BambuPlotter project (*.bplot)|*.bplot", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (fd.ShowModal() != wxID_OK)
        return;
    std::string err;
    if (!m_project.save(into_u8(fd.GetPath()), &err))
        set_status(err);
    else
        set_status("");
}

void PlotterModeDialog::on_open_project(wxCommandEvent &)
{
    wxFileDialog fd(this, _L("Open plotter project"), projects_dir(), "",
                    "BambuPlotter project (*.bplot)|*.bplot", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (fd.ShowModal() != wxID_OK)
        return;
    std::string err;
    PlotterProject proj;
    if (!proj.load(into_u8(fd.GetPath()), &err)) {
        set_status(err);
        return;
    }
    m_project     = std::move(proj);
    m_has_project = true;
    if (m_scale_spin)    m_scale_spin->SetValue(m_project.scale);
    if (m_offset_x_spin) m_offset_x_spin->SetValue(m_project.offset.x());
    if (m_offset_y_spin) m_offset_y_spin->SetValue(m_project.offset.y());
    set_status("");
    refresh_ui();
}

void PlotterModeDialog::on_generate_preview(wxCommandEvent &)
{
    if (!ensure_profile())
        return;
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
    if (auto *plater = wxGetApp().plater())
        plater->load_gcode(from_u8(preview_path));
}

void PlotterModeDialog::on_send(wxCommandEvent &)
{
    if (!ensure_profile())
        return;
    if (m_machine == nullptr) {
        set_status("no printer connected");
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
        m_machine, job, [this](const std::string &stage, int pct) {
            m_stats_label->SetLabelText(from_u8(stage) + wxString::Format(" (%d%%)", pct));
            wxYield();
        });
    if (!r.ok)
        set_status(r.error);
    else
        set_status("Job sent — the printer will run it. You can disconnect now.");
}

void PlotterModeDialog::on_dpi_changed(const wxRect &) { Refresh(); }

} } // namespace Slic3r::GUI
