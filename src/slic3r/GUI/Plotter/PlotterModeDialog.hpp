#ifndef slic3r_PlotterModeDialog_hpp_
#define slic3r_PlotterModeDialog_hpp_

#include <string>

#include "slic3r/GUI/GUI_Utils.hpp"
#include "libslic3r/Plotter/PlotterProject.hpp"
#include "libslic3r/Plotter/PlotterToolProfile.hpp"

class Button;
class wxStaticText;
class wxSpinCtrlDouble;

namespace Slic3r {

class MachineObject;

namespace GUI {

// Top-level Plotter Mode entry point. Bypasses the 3D slicing pipeline
// entirely: import an SVG, place it inside the calibrated plotting rectangle,
// preview the movement-only G-code, and send the validated job to the printer
// (Mac can disconnect after start). Calibration is handled by
// PlotterCalibrationDialog, launched from here when no valid profile exists.
class PlotterModeDialog : public DPIDialog
{
public:
    explicit PlotterModeDialog(wxWindow *parent, MachineObject *machine = nullptr);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void build_ui();
    void refresh_ui();
    void set_status(const std::string &msg);

    bool ensure_profile();               // load or run the calibration wizard
    void on_calibrate(wxCommandEvent &);
    void on_import_svg(wxCommandEvent &);
    void on_fit_to_area(wxCommandEvent &);
    void on_placement_changed();
    void on_save_project(wxCommandEvent &);
    void on_open_project(wxCommandEvent &);
    void on_generate_preview(wxCommandEvent &);
    void on_send(wxCommandEvent &);

    // Returns placed, optimized paths (paper space) or empty on failure.
    Plotter::PlotPaths build_placed_paths(std::string *error) const;

    MachineObject             *m_machine = nullptr;
    Plotter::PlotterToolProfile m_profile;
    Plotter::PlotterProject     m_project;
    bool                        m_has_project = false;

    wxStaticText     *m_profile_label{nullptr};
    Button           *m_btn_calibrate{nullptr};
    Button           *m_btn_import{nullptr};
    wxStaticText     *m_source_label{nullptr};
    wxSpinCtrlDouble *m_scale_spin{nullptr};
    wxSpinCtrlDouble *m_offset_x_spin{nullptr};
    wxSpinCtrlDouble *m_offset_y_spin{nullptr};
    Button           *m_btn_fit{nullptr};
    Button           *m_btn_save{nullptr};
    Button           *m_btn_open{nullptr};
    Button           *m_btn_preview{nullptr};
    Button           *m_btn_send{nullptr};
    wxStaticText     *m_stats_label{nullptr};
    wxStaticText     *m_status_label{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterModeDialog_hpp_
