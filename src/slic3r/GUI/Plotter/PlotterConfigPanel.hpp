#ifndef slic3r_PlotterConfigPanel_hpp_
#define slic3r_PlotterConfigPanel_hpp_

#include <wx/panel.h>

#include "libslic3r/Plotter/PlotterToolProfile.hpp"

class wxStaticText;
class wxSpinCtrlDouble;
class wxSpinDoubleEvent;
class Button;

namespace Slic3r { namespace GUI {

// Plotter CONFIGURATION for the Prepare-tab sidebar: calibration status +
// wizard launcher, pen tip size and speeds. Nothing else - artwork is
// manipulated on the plate with the native gizmos, actions live in the
// top-right Generate/Send buttons and the File menu.
class PlotterConfigPanel : public wxPanel
{
public:
    explicit PlotterConfigPanel(wxWindow *parent);

    // Re-reads the profile from disk and updates all labels/fields.
    void refresh();

private:
    void build_ui();
    void on_calibrate(wxCommandEvent &);
    void on_setting_changed();
    // Pushes the (possibly edited) profile to disk and re-syncs the app:
    // controller profile, plate overlay, button states.
    void apply_profile_to_app();

    Plotter::PlotterToolProfile m_profile;

    wxStaticText     *m_calibration_label = nullptr;
    Button           *m_btn_calibrate     = nullptr;
    wxSpinCtrlDouble *m_tip_width_spin    = nullptr;
    wxSpinCtrlDouble *m_travel_speed_spin = nullptr;
    wxSpinCtrlDouble *m_draw_speed_spin   = nullptr;
    wxSpinCtrlDouble *m_lift_speed_spin   = nullptr;
    bool              m_updating          = false;
};

} } // namespace Slic3r::GUI

#endif // slic3r_PlotterConfigPanel_hpp_
