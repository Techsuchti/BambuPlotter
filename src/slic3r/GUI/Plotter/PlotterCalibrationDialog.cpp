#include "PlotterCalibrationDialog.hpp"

#include <wx/checkbox.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"

namespace Slic3r { namespace GUI {

namespace {

// Capture slots of step 4, indexing m_btn_capture / m_capture_value.
enum CaptureSlot {
    CAPTURE_PAPER_ORIGIN = 0,
    CAPTURE_MIN_CORNER   = 1,
    CAPTURE_MAX_CORNER   = 2,
    CAPTURE_PEN_CONTACT  = 3,
    CAPTURE_PEN_UP       = 4,
};

constexpr int TIMER_INTERVAL_MS = 500;

void style_primary_button(Button *btn)
{
    StateColor btn_bg(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                      std::pair<wxColour, int>(wxColour(110, 140, 160), StateColor::Normal));
    StateColor btn_bd(std::pair<wxColour, int>(wxColour(110, 140, 160), StateColor::Normal));
    StateColor btn_text(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    btn->SetBackgroundColor(btn_bg);
    btn->SetBorderColor(btn_bd);
    btn->SetTextColor(btn_text);
}

wxStaticText *make_section_title(wxWindow *parent, const wxString &text)
{
    auto *title = new wxStaticText(parent, wxID_ANY, text);
    title->SetFont(Label::Head_14);
    return title;
}

} // namespace

PlotterCalibrationDialog::PlotterCalibrationDialog(wxWindow *parent, MachineObject *machine,
                                                   PlotterCalibrationController &controller)
    : DPIDialog(parent, wxID_ANY, _L("Plotter Calibration"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE)
    , m_controller(controller)
{
    m_controller.set_machine(machine);
    // Always re-read the profile from disk: the sidebar panel writes pen and
    // fill settings there between wizard sessions, and finalize() would
    // otherwise save a stale copy over them. load_profile() does NOT touch
    // the dead-reckoned position/captures - the state the shared controller
    // exists to preserve.
    m_controller.load_profile();

    SetBackgroundColour(*wxWHITE);
    build_ui();

    m_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &PlotterCalibrationDialog::on_timer, this);
    m_timer.Start(TIMER_INTERVAL_MS);

    refresh_ui();
    wxGetApp().UpdateDlgDarkUI(this);
}

PlotterCalibrationDialog::~PlotterCalibrationDialog()
{
    m_timer.Stop();
}

void PlotterCalibrationDialog::build_ui()
{
    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    // --- step 1: home ----------------------------------------------------
    main_sizer->Add(make_section_title(this, _L("Step 1: Home")), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));
    auto *warning = new wxStaticText(this, wxID_ANY,
        _L("Remove the pen before homing! Z homing presses the bed against the toolhead and would crush a mounted pen."));
    warning->SetFont(Label::Body_13);
    warning->SetForegroundColour(wxColour(208, 27, 27));
    warning->Wrap(FromDIP(420));
    main_sizer->Add(warning, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    auto *home_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_btn_home = new Button(this, _L("Home"));
    m_btn_home->SetMinSize(wxSize(FromDIP(80), FromDIP(28)));
    style_primary_button(m_btn_home);
    home_sizer->Add(m_btn_home, 0, wxALIGN_CENTER_VERTICAL);
    home_sizer->AddSpacer(FromDIP(8));
    // Position was only lost app-side (wizard reopened): re-establish it
    // with an absolute park move - no homing, pen may stay mounted.
    m_btn_sync = new Button(this, _L("Sync position"));
    m_btn_sync->SetMinSize(wxSize(FromDIP(110), FromDIP(28)));
    m_btn_sync->SetToolTip(_L("Re-establish the position with a park move (no homing; the pen can stay mounted). Available while the printer still knows its origin."));
    m_btn_sync->Bind(wxEVT_BUTTON, &PlotterCalibrationDialog::on_sync_position, this);
    home_sizer->Add(m_btn_sync, 0, wxALIGN_CENTER_VERTICAL);
    home_sizer->AddSpacer(FromDIP(12));
    m_state_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_state_label->SetFont(Label::Body_13);
    home_sizer->Add(m_state_label, 1, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(home_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    // --- step 2: jog -------------------------------------------------------
    main_sizer->Add(make_section_title(this, _L("Step 2: Jog")), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    const wxString increments[] = {wxT("0.1 mm"), wxT("1 mm"), wxT("10 mm")};
    m_increment_radio = new wxRadioBox(this, wxID_ANY, _L("Increment"), wxDefaultPosition, wxDefaultSize,
                                       3, increments, 3, wxRA_SPECIFY_COLS);
    m_increment_radio->SetSelection(1);
    main_sizer->Add(m_increment_radio, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    auto make_jog_button = [this](const wxString &label, char axis, double sign) {
        auto *btn = new Button(this, label);
        btn->SetMinSize(wxSize(FromDIP(56), FromDIP(28)));
        btn->Bind(wxEVT_BUTTON, [this, axis, sign](wxCommandEvent &) { on_jog(axis, sign); });
        return btn;
    };
    m_btn_x_plus  = make_jog_button(wxT("X+"), 'X', +1.);
    m_btn_x_minus = make_jog_button(wxT("X-"), 'X', -1.);
    m_btn_y_plus  = make_jog_button(wxT("Y+"), 'Y', +1.);
    m_btn_y_minus = make_jog_button(wxT("Y-"), 'Y', -1.);
    m_btn_z_plus  = make_jog_button(wxT("Z+"), 'Z', +1.);
    m_btn_z_minus = make_jog_button(wxT("Z-"), 'Z', -1.);

    auto *jog_grid = new wxGridSizer(3, 4, FromDIP(6), FromDIP(6));
    jog_grid->Add(0, 0);
    jog_grid->Add(m_btn_y_plus, 0, wxEXPAND);
    jog_grid->Add(0, 0);
    jog_grid->Add(m_btn_z_plus, 0, wxEXPAND);
    jog_grid->Add(m_btn_x_minus, 0, wxEXPAND);
    jog_grid->Add(0, 0);
    jog_grid->Add(m_btn_x_plus, 0, wxEXPAND);
    jog_grid->Add(0, 0);
    jog_grid->Add(0, 0);
    jog_grid->Add(m_btn_y_minus, 0, wxEXPAND);
    jog_grid->Add(0, 0);
    jog_grid->Add(m_btn_z_minus, 0, wxEXPAND);
    main_sizer->Add(jog_grid, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    m_position_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_position_label->SetFont(Label::Body_13);
    main_sizer->Add(m_position_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    // --- step 3: pen mounted -----------------------------------------------
    main_sizer->Add(make_section_title(this, _L("Step 3: Mount the pen")), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));
    m_pen_mounted_check = new wxCheckBox(this, wxID_ANY, _L("Pen is mounted (disables homing)"));
    m_pen_mounted_check->SetFont(Label::Body_13);
    m_pen_mounted_check->Bind(wxEVT_CHECKBOX, &PlotterCalibrationDialog::on_pen_mounted, this);
    main_sizer->Add(m_pen_mounted_check, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    // --- step 4: captures ---------------------------------------------------
    main_sizer->Add(make_section_title(this, _L("Step 4: Capture positions")), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));
    const wxString capture_labels[5] = {
        _L("Capture paper origin"),
        _L("Capture min corner"),
        _L("Capture max corner"),
        _L("Capture pen contact"),
        _L("Capture pen up"),
    };
    auto *capture_grid = new wxFlexGridSizer(5, 2, FromDIP(6), FromDIP(12));
    capture_grid->AddGrowableCol(1, 1);
    for (int i = 0; i < 5; ++i) {
        m_btn_capture[i] = new Button(this, capture_labels[i]);
        m_btn_capture[i]->SetMinSize(wxSize(FromDIP(170), FromDIP(28)));
        m_btn_capture[i]->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent &) { on_capture(i); });
        capture_grid->Add(m_btn_capture[i], 0, wxEXPAND);
        m_capture_value[i] = new wxStaticText(this, wxID_ANY, wxT("-"));
        m_capture_value[i]->SetFont(Label::Body_13);
        capture_grid->Add(m_capture_value[i], 0, wxALIGN_CENTER_VERTICAL);
    }
    main_sizer->Add(capture_grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    // --- step 5: pen offset (optional) --------------------------------------
    main_sizer->Add(make_section_title(this, _L("Step 5: Pen offset (optional)")), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));
    auto *offset_help = new wxStaticText(this, wxID_ANY,
        _L("Places artwork and the paper rectangle correctly on screen. Draw a small mark on the paper, "
           "jog the PEN TIP exactly over it and capture; then jog the NOZZLE over the same mark and capture. "
           "Skipping keeps the stored offset."));
    offset_help->SetFont(Label::Body_13);
    offset_help->Wrap(FromDIP(420));
    main_sizer->Add(offset_help, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    auto *offset_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_btn_offset_pen = new Button(this, _L("Capture pen on mark"));
    m_btn_offset_pen->SetMinSize(wxSize(FromDIP(170), FromDIP(28)));
    m_btn_offset_pen->Bind(wxEVT_BUTTON, &PlotterCalibrationDialog::on_capture_offset_pen, this);
    offset_sizer->Add(m_btn_offset_pen, 0, wxALIGN_CENTER_VERTICAL);
    offset_sizer->AddSpacer(FromDIP(8));
    m_btn_offset_nozzle = new Button(this, _L("Capture nozzle on mark"));
    m_btn_offset_nozzle->SetMinSize(wxSize(FromDIP(170), FromDIP(28)));
    m_btn_offset_nozzle->Bind(wxEVT_BUTTON, &PlotterCalibrationDialog::on_capture_offset_nozzle, this);
    offset_sizer->Add(m_btn_offset_nozzle, 0, wxALIGN_CENTER_VERTICAL);
    offset_sizer->AddSpacer(FromDIP(12));
    m_offset_value = new wxStaticText(this, wxID_ANY, wxT("-"));
    m_offset_value->SetFont(Label::Body_13);
    offset_sizer->Add(m_offset_value, 1, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(offset_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    // --- step 6: finish -----------------------------------------------------
    main_sizer->Add(make_section_title(this, _L("Step 6: Finish")), 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));
    m_btn_finish = new Button(this, _L("Finish"));
    m_btn_finish->SetMinSize(wxSize(FromDIP(80), FromDIP(28)));
    style_primary_button(m_btn_finish);
    m_btn_finish->Bind(wxEVT_BUTTON, &PlotterCalibrationDialog::on_finish, this);
    main_sizer->Add(m_btn_finish, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));

    m_status_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_status_label->SetFont(Label::Body_13);
    m_status_label->SetForegroundColour(wxColour(208, 27, 27));
    m_status_label->Wrap(FromDIP(420));
    main_sizer->Add(m_status_label, 0, wxEXPAND | wxALL, FromDIP(15));

    m_btn_home->Bind(wxEVT_BUTTON, &PlotterCalibrationDialog::on_home, this);

    SetSizer(main_sizer);
    Layout();
    Fit();
    CentreOnParent();
}

void PlotterCalibrationDialog::set_status(const std::string &msg)
{
    m_status_label->SetLabelText(from_u8(msg));
    Layout();
}

double PlotterCalibrationDialog::current_increment() const
{
    switch (m_increment_radio->GetSelection()) {
    case 0: return 0.1;
    case 2: return 10.;
    default: return 1.;
    }
}

void PlotterCalibrationDialog::refresh_ui()
{
    m_state_label->SetLabelText(from_u8(m_controller.state_description()));

    if (m_controller.position_known()) {
        const Vec3d &p = m_controller.position();
        m_position_label->SetLabelText(wxString::Format(_L("Position: X %.2f  Y %.2f  Z %.2f"), p.x(), p.y(), p.z()));
    } else {
        m_position_label->SetLabelText(_L("Position: unknown"));
    }

    const bool ready = m_controller.state() == PlotterCalibrationController::State::Ready;
    m_btn_home->Enable(m_controller.machine() != nullptr && !m_controller.pen_mounted());
    m_btn_sync->Enable(m_controller.machine() != nullptr && !ready &&
                       m_controller.state() == PlotterCalibrationController::State::NeedsHoming &&
                       m_controller.machine_reports_homed());
    for (Button *btn : {m_btn_x_plus, m_btn_x_minus, m_btn_y_plus, m_btn_y_minus, m_btn_z_plus, m_btn_z_minus})
        btn->Enable(ready);
    for (int i = 0; i < 5; ++i)
        m_btn_capture[i]->Enable(ready);

    m_btn_offset_pen->Enable(ready);
    m_btn_offset_nozzle->Enable(ready);
    {
        const Vec2d &off = m_controller.profile().pen_offset;
        wxString label = wxString::Format(wxT("(%.2f, %.2f)"), off.x(), off.y());
        if (m_controller.pen_mark_captured() != m_controller.nozzle_mark_captured())
            label += m_controller.pen_mark_captured() ? _L(" - now capture the nozzle") : _L(" - now capture the pen");
        m_offset_value->SetLabelText(label);
    }

    const auto &profile = m_controller.profile();
    m_capture_value[CAPTURE_PAPER_ORIGIN]->SetLabelText(
        wxString::Format(wxT("(%.2f, %.2f)"), profile.paper_origin.x(), profile.paper_origin.y()));
    m_capture_value[CAPTURE_MIN_CORNER]->SetLabelText(
        wxString::Format(wxT("(%.2f, %.2f)"), profile.min_x, profile.min_y));
    m_capture_value[CAPTURE_MAX_CORNER]->SetLabelText(
        wxString::Format(wxT("(%.2f, %.2f)"), profile.max_x, profile.max_y));
    m_capture_value[CAPTURE_PEN_CONTACT]->SetLabelText(
        wxString::Format(wxT("Z %.2f (draw Z %.2f)"), profile.pen_contact_z, profile.pen_down_z));
    m_capture_value[CAPTURE_PEN_UP]->SetLabelText(
        wxString::Format(wxT("Z %.2f"), profile.pen_up_z));

    Layout();
}

void PlotterCalibrationDialog::on_timer(wxTimerEvent &)
{
    m_controller.update();
    refresh_ui();
}

void PlotterCalibrationDialog::on_home(wxCommandEvent &)
{
    std::string err;
    if (!m_controller.home(&err)) {
        set_status(err);
        return;
    }
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_sync_position(wxCommandEvent &)
{
    std::string err;
    if (!m_controller.sync_position(&err)) {
        set_status(err);
        return;
    }
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_capture_offset_pen(wxCommandEvent &)
{
    std::string err;
    if (!m_controller.capture_pen_over_mark(&err)) {
        set_status(err);
        return;
    }
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_capture_offset_nozzle(wxCommandEvent &)
{
    std::string err;
    if (!m_controller.capture_nozzle_over_mark(&err)) {
        set_status(err);
        return;
    }
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_jog(char axis, double sign)
{
    std::string err;
    if (!m_controller.jog(axis, sign * current_increment(), &err)) {
        set_status(err);
        return;
    }
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_pen_mounted(wxCommandEvent &event)
{
    m_controller.set_pen_mounted(event.IsChecked());
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_capture(int which)
{
    std::string err;
    bool        ok = false;
    switch (which) {
    case CAPTURE_PAPER_ORIGIN: ok = m_controller.capture_paper_origin(&err); break;
    case CAPTURE_MIN_CORNER:   ok = m_controller.capture_min_corner(&err); break;
    case CAPTURE_MAX_CORNER:   ok = m_controller.capture_max_corner(&err); break;
    case CAPTURE_PEN_CONTACT:  ok = m_controller.capture_pen_contact(&err); break;
    case CAPTURE_PEN_UP:       ok = m_controller.capture_pen_up(&err); break;
    default: return;
    }
    if (!ok) {
        set_status(err);
        return;
    }
    set_status(std::string());
    refresh_ui();
}

void PlotterCalibrationDialog::on_finish(wxCommandEvent &)
{
    std::string err;
    if (!m_controller.finalize(&err)) {
        set_status(err);
        return;
    }
    EndModal(wxID_OK);
}

void PlotterCalibrationDialog::on_dpi_changed(const wxRect &)
{
    for (Button *btn : {m_btn_home, m_btn_finish, m_btn_x_plus, m_btn_x_minus, m_btn_y_plus, m_btn_y_minus,
                        m_btn_z_plus, m_btn_z_minus})
        btn->Rescale();
    for (int i = 0; i < 5; ++i)
        m_btn_capture[i]->Rescale();
    Layout();
    Fit();
    Refresh();
}

} } // namespace Slic3r::GUI
