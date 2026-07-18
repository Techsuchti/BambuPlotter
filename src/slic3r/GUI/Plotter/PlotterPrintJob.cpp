#include "PlotterPrintJob.hpp"

#include <atomic>
#include <fstream>

#include <boost/filesystem.hpp>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "slic3r/Utils/bambu_networking.hpp"

namespace Slic3r { namespace GUI {

PlotterPrintResult PlotterPrintJob::upload_and_start(MachineObject                *obj,
                                                     const Plotter::PlotterJob    &job,
                                                     const PlotterPrintProgressFn &progress)
{
    PlotterPrintResult result;

    if (!job.ok) {
        result.error = "job was not built successfully";
        return result;
    }
    if (obj == nullptr) {
        result.error = "no printer selected";
        return result;
    }
    NetworkAgent *agent = wxGetApp().getAgent();
    if (agent == nullptr) {
        result.error = "network agent is not available";
        return result;
    }
    if (obj->get_dev_ip().empty() || obj->get_access_code().empty()) {
        result.error = "printer is not connected over LAN with an access code";
        return result;
    }

    // Write the container to a temp file the agent uploads by path.
    const std::string tmp_path = (boost::filesystem::path(data_dir()) / "cache" / job.file_name).string();
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(boost::filesystem::path(tmp_path).parent_path(), ec);
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.error = "cannot write temporary job file: " + tmp_path;
            return result;
        }
        out.write(job.container.data(), std::streamsize(job.container.size()));
        out.close();
        if (!out) {
            result.error = "failed writing temporary job file";
            return result;
        }
    }

    BBL::PrintParams params;
    params.dev_id          = obj->get_dev_id();
    params.dev_name        = obj->get_dev_name();
    params.dev_ip          = obj->get_dev_ip();
    params.project_name    = job.file_name.substr(0, job.file_name.find(".gcode.3mf"));
    params.task_name       = params.project_name;
    params.filename        = tmp_path;
    params.config_filename = "";
    params.plate_index     = 1;
    params.ftp_folder      = "";
    params.username        = "bblp";
    params.password        = obj->get_access_code();
    params.use_ssl_for_ftp  = obj->local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt = obj->local_use_ssl_for_mqtt;
    params.connection_type = "lan";
    params.print_type      = "from_normal";
    params.task_bed_type   = "auto";

    // Everything the print pipeline could add is OFF: a plotter job carries no
    // filament and must not trigger calibration/AMS steps.
    params.task_bed_leveling     = false;
    params.task_flow_cali        = false;
    params.task_vibration_cali   = false;
    params.task_layer_inspect    = false;
    params.task_record_timelapse = false;
    params.task_use_ams          = false;
    params.ams_mapping           = "";
    params.ams_mapping2          = "";

    std::atomic<int> last_percent{-1};
    auto update_fn = [&](int stage, int code, const std::string &msg) {
        if (progress) {
            // BBL stages are negative-to-positive; expose a coarse percent.
            const int pct = stage >= 0 ? std::min(99, stage) : 0;
            if (pct != last_percent.load()) {
                last_percent = pct;
                progress(msg.empty() ? "Uploading…" : msg, pct);
            }
        }
    };
    auto cancel_fn = []() { return false; };

    const int rc = agent->start_local_print(params, update_fn, cancel_fn);
    if (rc != 0) {
        result.error = "printer rejected the job (network error code " + std::to_string(rc) + ")";
        return result;
    }
    if (progress)
        progress("Print started", 100);
    result.ok = true;
    return result;
}

} } // namespace Slic3r::GUI
