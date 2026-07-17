#include "PlotterJobBuilder.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include <miniz.h>
#include <openssl/md5.h>

#include "PlotterSafetyValidator.hpp"
#include "libslic3r_version.h"

namespace Slic3r { namespace Plotter {

namespace {

const char *PLATE_GCODE_ENTRY = "Metadata/plate_1.gcode";
const char *PLATE_MD5_ENTRY   = "Metadata/plate_1.gcode.md5";

std::string md5_upper_hex(const std::string &data)
{
    unsigned char digest[16];
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, data.data(), data.size());
    MD5_Final(digest, &ctx);
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(32);
    for (unsigned char b : digest) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0xf]);
    }
    return out;
}

bool read_file(const std::string &path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string sanitize_name(const std::string &name)
{
    std::string out;
    for (char c : name)
        out.push_back((std::isalnum((unsigned char) c) || c == '-' || c == '_') ? c : '_');
    if (out.empty())
        out = "plot";
    return out;
}

} // namespace

int PlotterJobBuilder::estimate_seconds(const GCodeGenResult &gen, const PlotterToolProfile &profile)
{
    const double lift_distance = 2. * double(gen.path_count) * (profile.pen_up_z - profile.pen_down_z);
    const double motion = gen.draw_length / profile.draw_speed +
                          gen.travel_length / profile.travel_speed +
                          lift_distance / profile.lift_speed;
    // Firmware prep phase plus accel/jerk losses.
    return int(std::lround(motion * 1.15)) + 15;
}

std::string PlotterJobBuilder::make_header(const GCodeGenResult &gen, const PlotterToolProfile &profile,
                                           int estimated_seconds)
{
    const int minutes = estimated_seconds / 60;
    const int seconds = estimated_seconds % 60;
    std::ostringstream h;
    h << "; HEADER_BLOCK_START\n"
      << "; BambuStudio " << SLIC3R_VERSION << "\n"
      << "; model printing time: " << minutes << "m " << seconds << "s; total estimated time: "
      << minutes << "m " << seconds << "s\n"
      << "; total layer number: 2\n"
      << "; total filament length [mm] : 0.00\n"
      << "; total filament volume [cm^3] : 0.00\n"
      << "; total filament weight [g] : 0.00\n"
      << "; filament_density: 1.24\n"
      << "; filament_diameter: 1.75\n"
      << "; max_z_height: " << int(std::ceil(profile.pen_up_z)) << ".00\n"
      << "; filament: 1\n"
      << "; HEADER_BLOCK_END\n";
    return h.str();
}

PlotterJob PlotterJobBuilder::build(const PlotPaths          &paths,
                                    const PlotterToolProfile &profile,
                                    const std::string        &job_name,
                                    const std::string        &resources_plotter_dir)
{
    PlotterJob job;

    const GCodeGenResult gen = PlotterGCodeGenerator::generate(paths, profile);
    if (!gen.ok) {
        job.error = gen.error;
        return job;
    }

    std::string config_block;
    if (!read_file(resources_plotter_dir + "/plate_config_block.txt", config_block)) {
        job.error = "missing resource: plate_config_block.txt in " + resources_plotter_dir;
        return job;
    }
    std::string template_3mf;
    if (!read_file(resources_plotter_dir + "/plot_container_template.gcode.3mf", template_3mf)) {
        job.error = "missing resource: plot_container_template.gcode.3mf in " + resources_plotter_dir;
        return job;
    }

    job.estimated_seconds = estimate_seconds(gen, profile);
    job.draw_length       = gen.draw_length;
    job.travel_length     = gen.travel_length;

    // Assemble the plate G-code. The firmware requires the HEADER and CONFIG
    // blocks; the executable block carries M73 progress so the printer's
    // progress bar and remaining-time display work.
    std::ostringstream plate;
    plate << make_header(gen, profile, job.estimated_seconds)
          << "\n"
          << config_block
          << "\n\n"
          << "; EXECUTABLE_BLOCK_START\n"
          << "M73 P0 R" << std::max(1, (job.estimated_seconds + 59) / 60) << "\n"
          << "M73 C2\n"
          << gen.gcode
          << "M73 P100 R0\n"
          << "; EXECUTABLE_BLOCK_END\n";
    job.plate_gcode = plate.str();

    // Final strict gate on the EXACT bytes that will be uploaded: the header
    // and config sections are pure comments, so the validator parses the
    // whole plate G-code. Building fails (never warns) on any violation.
    const ValidationResult validation = PlotterSafetyValidator::validate(job.plate_gcode, profile);
    if (!validation.ok) {
        job.error = validation.summary();
        return job;
    }

    // Swap the plate G-code and its md5 into the verified container template.
    mz_zip_archive reader;
    std::memset(&reader, 0, sizeof(reader));
    if (!mz_zip_reader_init_mem(&reader, template_3mf.data(), template_3mf.size(), 0)) {
        job.error = "container template is not a readable zip archive";
        return job;
    }

    mz_zip_archive writer;
    std::memset(&writer, 0, sizeof(writer));
    if (!mz_zip_writer_init_heap(&writer, 0, template_3mf.size() + job.plate_gcode.size())) {
        mz_zip_reader_end(&reader);
        job.error = "cannot initialize container writer";
        return job;
    }

    const std::string md5 = md5_upper_hex(job.plate_gcode);
    bool ok = true;
    const mz_uint file_count = mz_zip_reader_get_num_files(&reader);
    for (mz_uint i = 0; ok && i < file_count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader, i, &stat)) {
            ok = false;
            break;
        }
        if (stat.m_is_directory)
            continue;
        const std::string name = stat.m_filename;
        if (name == PLATE_GCODE_ENTRY) {
            ok = mz_zip_writer_add_mem(&writer, name.c_str(), job.plate_gcode.data(),
                                       job.plate_gcode.size(), MZ_DEFAULT_COMPRESSION);
        } else if (name == PLATE_MD5_ENTRY) {
            ok = mz_zip_writer_add_mem(&writer, name.c_str(), md5.data(), md5.size(),
                                       MZ_DEFAULT_COMPRESSION);
        } else {
            size_t size = 0;
            void *data = mz_zip_reader_extract_to_heap(&reader, i, &size, 0);
            if (data == nullptr) {
                ok = false;
                break;
            }
            ok = mz_zip_writer_add_mem(&writer, name.c_str(), data, size, MZ_DEFAULT_COMPRESSION);
            mz_free(data);
        }
    }

    void  *buf  = nullptr;
    size_t size = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&writer, &buf, &size);
    mz_zip_writer_end(&writer);
    mz_zip_reader_end(&reader);
    if (!ok || buf == nullptr) {
        if (buf != nullptr)
            mz_free(buf);
        job.error = "failed to assemble the gcode.3mf container";
        return job;
    }
    job.container.assign(static_cast<const char *>(buf), size);
    mz_free(buf);

    job.file_name = sanitize_name(job_name) + ".gcode.3mf";
    job.ok        = true;
    return job;
}

} } // namespace Slic3r::Plotter
