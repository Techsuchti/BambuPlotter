#ifndef slic3r_PlotterPanel_hpp_
#define slic3r_PlotterPanel_hpp_

#include <string>

#include <wx/panel.h>

#include "libslic3r/Plotter/PlotterProject.hpp"
#include "libslic3r/Plotter/PlotterToolProfile.hpp"

class Button;
class wxStaticText;
class wxSpinCtrlDouble;

namespace Slic3r {

class MachineObject;

namespace GUI {

// Plotter Mode sidebar panel. Lives inside the Prepare tab's Sidebar and is
// swapped in for the Printer/Filament/Process sections when the user flips
// the "3D Print / Plotter" switch at the top of the sidebar. Reuses the
// existing 3D plate view: the calibrated plotting rectangle and the placed
// SVG strokes are drawn directly on the current plate as overlays.
class PlotterPanel : public wxPanel
{
public:
    explicit PlotterPanel(wxWindow *parent);

    // Called by the Sidebar when the mode switch flips. Activating reloads
    // the calibration profile from disk and shows the plate overlays;
    // deactivating hides them again.
    void set_active(bool active);

    // Native File-menu integration: Plater::save_project/load_project route
    // here. save returns wxID_YES / wxID_CANCEL; open accepts a .bplot or
    // .svg path, or empty to ask the user.
    int  save_project_ui(bool save_as);
    bool open_project_ui(const wxString &filename);

private:
    void build_ui();
    void refresh_ui();
    void set_status(const std::string &msg);
    void update_plate_overlay();
    void fit_to_area();
    bool import_svg_path(const std::string &path);

    MachineObject *selected_machine() const;

    void on_calibrate(wxCommandEvent &);
    void on_import_svg(wxCommandEvent &);
    void on_fit_to_area(wxCommandEvent &);
    void on_placement_changed();
    void on_generate_preview(wxCommandEvent &);
    void on_send(wxCommandEvent &);

    // Returns placed, optimized paths (paper space) or empty on failure.
    Plotter::PlotPaths build_placed_paths(std::string *error) const;

    bool                        m_active = false;
    Plotter::PlotterToolProfile m_profile;
    Plotter::PlotterProject     m_project;
    bool                        m_has_project = false;
    std::string                 m_project_path; // last saved/opened .bplot

    wxStaticText     *m_profile_label{nullptr};
    Button           *m_btn_calibrate{nullptr};
    Button           *m_btn_import{nullptr};
    wxStaticText     *m_source_label{nullptr};
    wxSpinCtrlDouble *m_scale_spin{nullptr};
    wxSpinCtrlDouble *m_offset_x_spin{nullptr};
    wxSpinCtrlDouble *m_offset_y_spin{nullptr};
    Button           *m_btn_fit{nullptr};
    Button           *m_btn_preview{nullptr};
    Button           *m_btn_send{nullptr};
    wxStaticText     *m_stats_label{nullptr};
    wxStaticText     *m_status_label{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_PlotterPanel_hpp_
