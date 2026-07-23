#ifndef slic3r_PlotterCalibrationDialog_hpp_
#define slic3r_PlotterCalibrationDialog_hpp_

#include <wx/timer.h>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/Plotter/PlotterCalibrationController.hpp"

class Button;
class wxCheckBox;
class wxRadioBox;
class wxStaticText;

namespace Slic3r {

class MachineObject;

namespace GUI {

// Modal wizard that walks the user through the manual plotter calibration:
//   1. home the printer with the pen removed
//   2. jog the toolhead at a selectable increment
//   3. confirm the pen is mounted (locks out homing)
//   4. capture paper origin / min corner / max corner / pen contact / pen up
//   5. finalize (validates + persists the PlotterToolProfile)
// The dialog owns the PlotterCalibrationController and polls its update()
// on a timer; error strings returned by controller calls are shown in a
// status label at the bottom.
class PlotterCalibrationDialog : public DPIDialog
{
public:
    PlotterCalibrationDialog(wxWindow *parent, MachineObject *machine);
    ~PlotterCalibrationDialog() override;

    PlotterCalibrationController       &controller() { return m_controller; }
    const PlotterCalibrationController &controller() const { return m_controller; }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void build_ui();
    void refresh_ui();
    void set_status(const std::string &msg);

    double current_increment() const;
    void   on_timer(wxTimerEvent &event);
    void   on_home(wxCommandEvent &event);
    void   on_sync_position(wxCommandEvent &event);
    void   on_capture_offset_pen(wxCommandEvent &event);
    void   on_capture_offset_nozzle(wxCommandEvent &event);
    void   on_jog(char axis, double sign);
    void   on_pen_mounted(wxCommandEvent &event);
    void   on_capture(int which);
    void   on_finish(wxCommandEvent &event);

    PlotterCalibrationController m_controller;
    wxTimer                      m_timer;

    // step 1
    wxStaticText *m_state_label{nullptr};
    Button       *m_btn_home{nullptr};
    Button       *m_btn_sync{nullptr};
    // step 5: pen offset
    Button       *m_btn_offset_pen{nullptr};
    Button       *m_btn_offset_nozzle{nullptr};
    wxStaticText *m_offset_value{nullptr};
    // step 2
    wxRadioBox   *m_increment_radio{nullptr};
    wxStaticText *m_position_label{nullptr};
    Button       *m_btn_x_plus{nullptr};
    Button       *m_btn_x_minus{nullptr};
    Button       *m_btn_y_plus{nullptr};
    Button       *m_btn_y_minus{nullptr};
    Button       *m_btn_z_plus{nullptr};
    Button       *m_btn_z_minus{nullptr};
    // step 3
    wxCheckBox   *m_pen_mounted_check{nullptr};
    // step 4
    Button       *m_btn_capture[5]{nullptr, nullptr, nullptr, nullptr, nullptr};
    wxStaticText *m_capture_value[5]{nullptr, nullptr, nullptr, nullptr, nullptr};
    // step 5
    Button       *m_btn_finish{nullptr};
    wxStaticText *m_status_label{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterCalibrationDialog_hpp_
