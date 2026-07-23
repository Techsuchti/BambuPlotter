#include "PlotterConfigPanel.hpp"

#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/StaticLine.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationController.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationDialog.hpp"
#include "slic3r/GUI/Plotter/PlotterController.hpp"

using namespace Slic3r::Plotter;

namespace Slic3r { namespace GUI {

PlotterConfigPanel::PlotterConfigPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY)
{
    // Constructed while the Plater is still being built - no plater access
    // here; app-wide syncing happens in user-triggered handlers only.
    m_profile.load(PlotterCalibrationController::profile_path());
    SetBackgroundColour(*wxWHITE);
    build_ui();
    refresh();
}

void PlotterConfigPanel::build_ui()
{
    const int em  = wxGetApp().em_unit();
    const int gap = FromDIP(10);

    auto *root = new wxBoxSizer(wxVERTICAL);

    // Section title bars mirroring the native sidebar sections.
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
    m_calibration_label = new wxStaticText(this, wxID_ANY, "");
    root->Add(m_calibration_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    m_btn_calibrate = new Button(this, _L("Calibrate…"));
    m_btn_calibrate->Bind(wxEVT_BUTTON, &PlotterConfigPanel::on_calibrate, this);
    root->Add(m_btn_calibrate, 0, wxLEFT | wxTOP | wxBOTTOM, gap);

    // --- Pen --------------------------------------------------------------
    add_title(_L("Pen"));
    // Space-between rows: label column grows, controls hug the right edge.
    auto *grid = new wxFlexGridSizer(4, 2, FromDIP(6), FromDIP(8));
    grid->AddGrowableCol(0, 1);
    auto add_spin = [&](const wxString &label, double lo, double hi, double inc) {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto *s = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxSize(FromDIP(110), -1));
        s->SetRange(lo, hi);
        s->SetDigits(2);
        s->SetIncrement(inc);
        s->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) { on_setting_changed(); });
        grid->Add(s, 0, wxALIGN_RIGHT);
        return s;
    };
    m_tip_width_spin    = add_spin(_L("Tip width (mm)"),        0.1,   5.0, 0.05);
    m_travel_speed_spin = add_spin(_L("Travel speed (mm/s)"),   5.0, 300.0, 5.0);
    m_draw_speed_spin   = add_spin(_L("Draw speed (mm/s)"),     1.0, 300.0, 5.0);
    m_lift_speed_spin   = add_spin(_L("Pen lift speed (mm/s)"), 1.0,  50.0, 1.0);
    root->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, gap);

    // --- Fill (solid SVG areas) --------------------------------------------
    add_title(_L("Fill"));
    m_fill_check = new wxCheckBox(this, wxID_ANY, _L("Fill solid areas (hatching)"));
    m_fill_check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { on_setting_changed(); });
    root->Add(m_fill_check, 0, wxLEFT | wxRIGHT | wxTOP, gap);

    auto *fgrid = new wxFlexGridSizer(3, 2, FromDIP(6), FromDIP(8));
    fgrid->AddGrowableCol(0, 1);
    fgrid->Add(new wxStaticText(this, wxID_ANY, _L("Pattern")), 0, wxALIGN_CENTER_VERTICAL);
    m_pattern_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(110), -1));
    m_pattern_choice->Append(_L("Lines"));
    m_pattern_choice->Append(_L("Concentric"));
    m_pattern_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { on_setting_changed(); });
    fgrid->Add(m_pattern_choice, 0, wxALIGN_RIGHT);
    auto add_fill_spin = [&](const wxString &label, double lo, double hi, double inc) {
        fgrid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto *s = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxSize(FromDIP(110), -1));
        s->SetRange(lo, hi);
        s->SetDigits(2);
        s->SetIncrement(inc);
        s->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &) { on_setting_changed(); });
        fgrid->Add(s, 0, wxALIGN_RIGHT);
        return s;
    };
    // Spacing = tip width x factor: <1 overlaps for solid coverage.
    m_fill_spacing_spin = add_fill_spin(_L("Spacing (x tip width)"), 0.3, 3.0, 0.05);
    m_fill_angle_spin   = add_fill_spin(_L("Hatch angle (deg)"),     0.0, 180.0, 5.0);
    root->Add(fgrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, gap);

    SetSizer(root);
}

void PlotterConfigPanel::refresh()
{
    m_updating = true;
    m_profile.load(PlotterCalibrationController::profile_path());

    if (m_profile.is_valid()) {
        m_calibration_label->SetLabel(wxString::Format(
            _L("Calibrated. Plot area: %.0f x %.0f mm"),
            m_profile.plot_width(), m_profile.plot_height()));
    } else {
        m_calibration_label->SetLabel(
            _L("Not calibrated. Calibration unlocks plotting."));
    }
    m_calibration_label->Wrap(GetParent() != nullptr ? GetParent()->GetSize().x - FromDIP(30) : FromDIP(280));

    m_tip_width_spin->SetValue(m_profile.pen_tip_width);
    m_travel_speed_spin->SetValue(m_profile.travel_speed);
    m_draw_speed_spin->SetValue(m_profile.draw_speed);
    m_lift_speed_spin->SetValue(m_profile.lift_speed);
    m_fill_check->SetValue(m_profile.fill_enabled);
    m_pattern_choice->SetSelection(m_profile.hatch_pattern == 1 ? 1 : 0);
    m_fill_spacing_spin->SetValue(m_profile.hatch_spacing_factor);
    m_fill_angle_spin->SetValue(m_profile.hatch_angle);
    const bool fill_on = m_profile.fill_enabled;
    m_pattern_choice->Enable(fill_on);
    m_fill_spacing_spin->Enable(fill_on);
    m_fill_angle_spin->Enable(fill_on);

    m_updating = false;
    Layout();
}

void PlotterConfigPanel::on_calibrate(wxCommandEvent &)
{
    DeviceManager *dev = wxGetApp().getDeviceManager();
    MachineObject *machine = dev == nullptr ? nullptr : dev->get_selected_machine();
    if (machine == nullptr) {
        MessageDialog dlg(this, _L("No printer connected. Connect your A1 mini in the Device tab first."),
                          _L("Plotter calibration"), wxOK | wxICON_WARNING);
        dlg.ShowModal();
        return;
    }

    PlotterCalibrationDialog dlg(wxGetApp().mainframe, machine);
    dlg.ShowModal();

    // Whether finished or aborted, re-read what is on disk and re-sync.
    refresh();
    apply_profile_to_app();
}

void PlotterConfigPanel::on_setting_changed()
{
    if (m_updating)
        return;
    m_profile.pen_tip_width = m_tip_width_spin->GetValue();
    m_profile.travel_speed  = m_travel_speed_spin->GetValue();
    m_profile.draw_speed    = m_draw_speed_spin->GetValue();
    m_profile.lift_speed    = m_lift_speed_spin->GetValue();
    m_profile.fill_enabled         = m_fill_check->GetValue();
    m_profile.hatch_pattern        = m_pattern_choice->GetSelection() == 1 ? 1 : 0;
    m_profile.hatch_spacing_factor = m_fill_spacing_spin->GetValue();
    m_profile.hatch_angle          = m_fill_angle_spin->GetValue();
    m_pattern_choice->Enable(m_profile.fill_enabled);
    m_fill_spacing_spin->Enable(m_profile.fill_enabled);
    m_fill_angle_spin->Enable(m_profile.fill_enabled);

    const std::string path = PlotterCalibrationController::profile_path();
    boost::system::error_code ec;
    boost::filesystem::create_directories(boost::filesystem::path(path).parent_path(), ec);
    std::string error;
    if (!m_profile.save(path, &error)) {
        MessageDialog dlg(this, _L("Failed to save the plotter settings:") + " " + from_u8(error),
                          _L("Plotter settings"), wxOK | wxICON_ERROR);
        dlg.ShowModal();
        return;
    }
    apply_profile_to_app();
}

void PlotterConfigPanel::apply_profile_to_app()
{
    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    PlotterController *controller = plater->plotter_controller();
    controller->reload_profile(); // also invalidates any generated plot

    // Calibrated paper rectangle on the plate (pen-tip bed coordinates).
    const bool valid = controller->profile_valid();
    plater->get_partplate_list().set_plot_rectangle(
        valid ? controller->profile().pen_rect_on_bed() : BoundingBoxf(), valid);

    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->update_slice_print_status(MainFrame::eEventParamUpdate, true, true);
}

} } // namespace Slic3r::GUI
