#include <catch2/catch.hpp>

#include <miniz.h>

#include "libslic3r/TriangleMesh.hpp"

#include "libslic3r/Plotter/ArtworkMesh.hpp"
#include "libslic3r/Plotter/FillHatcher.hpp"
#include "libslic3r/Plotter/PathFlattener.hpp"
#include "libslic3r/Plotter/PathOptimizer.hpp"
#include "libslic3r/Plotter/PlotterArtwork.hpp"
#include "libslic3r/Plotter/PlotterGCodeGenerator.hpp"
#include "libslic3r/Plotter/PlotterJobBuilder.hpp"
#include "libslic3r/Plotter/PlotterPath.hpp"
#include "libslic3r/Plotter/PlotterProject.hpp"
#include "libslic3r/Plotter/PlotterSafetyValidator.hpp"
#include "libslic3r/Plotter/PlotterToolProfile.hpp"
#include "libslic3r/Plotter/SvgPlotImporter.hpp"

using namespace Slic3r;
using namespace Slic3r::Plotter;

namespace {

// A calibrated profile mimicking an A1 mini with a side-mounted pen.
PlotterToolProfile test_profile()
{
    PlotterToolProfile p;
    p.calibrated    = true;
    p.pen_offset    = Vec2d(35., 0.);
    p.paper_origin  = Vec2d(30., 30.);
    p.min_x         = 25.;
    p.max_x         = 140.;
    p.min_y         = 25.;
    p.max_y         = 140.;
    p.pen_up_z      = 10.;
    p.pen_contact_z = 3.;
    p.pen_down_z    = 2.5;
    return p;
}

bool contains_line(const std::string &text, const std::string &line)
{
    std::istringstream is(text);
    std::string        l;
    while (std::getline(is, l))
        if (l == line)
            return true;
    return false;
}

} // namespace

TEST_CASE("Plotter: profile validity", "[Plotter]")
{
    PlotterToolProfile p = test_profile();
    REQUIRE(p.is_valid());

    SECTION("uncalibrated profile is rejected") {
        p.calibrated = false;
        REQUIRE_FALSE(p.is_valid());
    }
    SECTION("inverted limits are rejected") {
        p.min_x = p.max_x + 1.;
        REQUIRE_FALSE(p.is_valid());
    }
    SECTION("pen height ordering is enforced") {
        p.pen_down_z = p.pen_up_z + 1.;
        REQUIRE_FALSE(p.is_valid());
    }
    SECTION("negative pen-down floor is rejected") {
        p.pen_down_z = -0.5;
        p.pen_contact_z = 0.;
        REQUIRE_FALSE(p.is_valid());
    }
    SECTION("paper origin must lie inside the limits") {
        p.paper_origin = Vec2d(0., 0.);
        REQUIRE_FALSE(p.is_valid());
    }
    SECTION("speed caps are enforced") {
        p.travel_speed = 1000.;
        REQUIRE_FALSE(p.is_valid());
    }
}

TEST_CASE("Plotter: profile JSON round trip", "[Plotter]")
{
    const PlotterToolProfile p = test_profile();
    const std::string json = p.serialize_json();

    PlotterToolProfile q;
    std::string        error;
    REQUIRE(q.deserialize_json(json, &error));
    CHECK(error.empty());
    CHECK(q.calibrated == p.calibrated);
    CHECK(q.paper_origin == p.paper_origin);
    CHECK(q.pen_offset == p.pen_offset);
    CHECK(q.min_x == Approx(p.min_x));
    CHECK(q.max_y == Approx(p.max_y));
    CHECK(q.pen_up_z == Approx(p.pen_up_z));
    CHECK(q.pen_contact_z == Approx(p.pen_contact_z));
    CHECK(q.pen_down_z == Approx(p.pen_down_z));
    CHECK(q.allow_homing_in_job == p.allow_homing_in_job);
    REQUIRE(q.is_valid());

    PlotterToolProfile bad;
    REQUIRE_FALSE(bad.deserialize_json("{ not json", &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Plotter: PathFlattener stays within tolerance", "[Plotter]")
{
    // Cubic approximation of a quarter circle of radius 10 around (0,0):
    // (10,0) -> (0,10), control points at k = 0.5523 * r.
    const double       k = 5.522847498;
    const PathFlattener flattener(0.05);
    std::vector<Vec2d> pts;
    flattener.flatten_cubic(pts, Vec2d(10., 0.), Vec2d(10., k), Vec2d(k, 10.), Vec2d(0., 10.));

    REQUIRE(pts.size() >= 4);
    CHECK((pts.front() - Vec2d(10., 0.)).norm() < 1e-9);
    CHECK((pts.back() - Vec2d(0., 10.)).norm() < 1e-9);
    // Every vertex must lie near the arc; the bezier itself deviates from a
    // true circle by < 0.02 % of r.
    for (const Vec2d &pt : pts)
        CHECK(pt.norm() == Approx(10.).margin(0.06));
    // And the chords must not cut inside the arc by more than the tolerance.
    for (size_t i = 1; i < pts.size(); ++i) {
        const Vec2d mid = 0.5 * (pts[i - 1] + pts[i]);
        CHECK(mid.norm() > 10. - 0.06);
    }
}

TEST_CASE("Plotter: SVG import preserves open paths", "[Plotter]")
{
    // 100x100 mm document: one open polyline stroke and one closed rectangle.
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">
        <polyline points="10,10 50,10 50,50" fill="none" stroke="black" stroke-width="1"/>
        <rect x="20" y="20" width="30" height="20" fill="none" stroke="black" stroke-width="1"/>
    </svg>)";

    const SvgImportResult result = SvgPlotImporter::import_memory(svg);
    REQUIRE(result.ok);
    CHECK(result.width == Approx(100.));
    CHECK(result.height == Approx(100.));
    REQUIRE(result.paths.size() == 2);

    const auto open_it = std::find_if(result.paths.begin(), result.paths.end(),
                                      [](const PlotPath &p) { return !p.closed; });
    const auto closed_it = std::find_if(result.paths.begin(), result.paths.end(),
                                        [](const PlotPath &p) { return p.closed; });
    REQUIRE(open_it != result.paths.end());
    REQUIRE(closed_it != result.paths.end());

    // Y flip: SVG (10,10) in a 100-high document becomes (10,90).
    CHECK((open_it->points.front() - Vec2d(10., 90.)).norm() < 1e-3);
    CHECK((open_it->points.back() - Vec2d(50., 50.)).norm() < 1e-3);
    // The open path must NOT have been closed or healed.
    CHECK((open_it->points.front() - open_it->points.back()).norm() > 1.);

    // Rectangle: 4 corners, no duplicated closing point.
    CHECK(closed_it->points.size() == 4);
}

TEST_CASE("Plotter: SVG import falls back to fill-only shapes", "[Plotter]")
{
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="50mm" height="50mm" viewBox="0 0 50 50">
        <rect x="10" y="10" width="20" height="20" fill="red"/>
    </svg>)";
    const SvgImportResult result = SvgPlotImporter::import_memory(svg);
    REQUIRE(result.ok);
    REQUIRE(result.paths.size() == 1);
    CHECK(result.paths.front().closed);
}

TEST_CASE("Plotter: SVG import rejects empty documents", "[Plotter]")
{
    const SvgImportResult result = SvgPlotImporter::import_memory("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("Plotter: PathOptimizer reduces pen-up travel", "[Plotter]")
{
    // Three horizontal strokes deliberately ordered worst-first.
    PlotPaths paths;
    for (double y : {100., 10., 55.}) {
        PlotPath p;
        p.points = {Vec2d(0., y), Vec2d(50., y)};
        paths.emplace_back(p);
    }
    const Vec2d start(0., 0.);
    const double travel_before = pen_up_travel(paths, start);

    PlotPaths optimized = PathOptimizer::optimize(paths, start);
    REQUIRE(optimized.size() == paths.size());
    const double travel_after = pen_up_travel(optimized, start);
    CHECK(travel_after < travel_before);
    // Best order is bottom-up with alternating direction; the travel must at
    // least beat the trivial 100+145+100 ordering by a wide margin.
    CHECK(travel_after < 120.);

    // Geometry is preserved (same multiset of segment lengths).
    double len_before = 0., len_after = 0.;
    for (const PlotPath &p : paths) len_before += p.length();
    for (const PlotPath &p : optimized) len_after += p.length();
    CHECK(len_after == Approx(len_before));
}

TEST_CASE("Plotter: PathOptimizer keeps closed paths closed", "[Plotter]")
{
    PlotPath square;
    square.closed = true;
    square.points = {Vec2d(0., 0.), Vec2d(10., 0.), Vec2d(10., 10.), Vec2d(0., 10.)};
    PlotPath line;
    line.points = {Vec2d(20., 0.), Vec2d(30., 0.)};

    PlotPaths optimized = PathOptimizer::optimize({square, line}, Vec2d(0., 0.));
    REQUIRE(optimized.size() == 2);
    int closed_count = 0;
    for (const PlotPath &p : optimized)
        if (p.closed)
            ++closed_count;
    CHECK(closed_count == 1);
}

TEST_CASE("Plotter: PathOptimizer preserves every path (concentric squares regression)", "[Plotter]")
{
    // Five concentric closed squares: the previous chain_polylines-based
    // implementation dropped/duplicated closed paths whose first == last
    // endpoint collided in its KD tree. All five must survive, unchanged in
    // total length, all still closed.
    PlotPaths paths;
    const Vec2d center(30., 30.);
    for (int i = 0; i < 5; ++i) {
        const double size = 20. + 10. * i;
        const Vec2d corner = center - Vec2d(size / 2., size / 2.);
        PlotPath sq;
        sq.closed = true;
        sq.points = {corner, corner + Vec2d(size, 0.), corner + Vec2d(size, size), corner + Vec2d(0., size)};
        paths.emplace_back(sq);
    }
    double len_before = 0.;
    for (const PlotPath &p : paths) len_before += p.length() + (p.points.front() - p.points.back()).norm();

    PlotPaths optimized = PathOptimizer::optimize(paths, Vec2d(0., 0.));
    REQUIRE(optimized.size() == 5);
    double len_after = 0.;
    for (const PlotPath &p : optimized) {
        CHECK(p.closed);
        CHECK(p.points.size() == 4);
        len_after += p.length() + (p.points.front() - p.points.back()).norm();
    }
    CHECK(len_after == Approx(len_before));
    // Entry-vertex rotation: after the initial ~28.3 mm approach from (0,0),
    // each square must be entered at the corner nearest the previous one
    // (4 hops of ~7.07 mm), so total travel stays near 56.6 mm.
    CHECK(pen_up_travel(optimized, Vec2d(0., 0.)) < 60.);
}

TEST_CASE("Plotter: G-code generator emits movement-only output", "[Plotter]")
{
    const PlotterToolProfile profile = test_profile();
    const GCodeGenResult result = PlotterGCodeGenerator::generate(
        PlotterGCodeGenerator::test_square(20., Vec2d(10., 10.)), profile);

    REQUIRE(result.ok);
    REQUIRE(result.error.empty());
    CHECK(result.path_count == 1);
    CHECK(result.draw_length == Approx(80.));

    // Structure: absolute mode, pen up before any XY, pen down at start.
    CHECK(contains_line(result.gcode, "G90"));
    CHECK(contains_line(result.gcode, "G1 Z10 F600"));
    CHECK(contains_line(result.gcode, "G1 Z2.5 F600"));
    // Square corners in machine space: paper (10,10) + origin (30,30) = (40,40).
    CHECK(contains_line(result.gcode, "G1 X40 Y40 F4800"));
    CHECK(contains_line(result.gcode, "G1 X60 Y40 F1800"));
    // Closed square: returns to the first corner.
    const size_t first_corner = result.gcode.find("G1 X40 Y40 F1800");
    CHECK(first_corner != std::string::npos);

    // No homing by default, no forbidden commands anywhere.
    for (const std::string &forbidden : {"G28", "M109", "M190", "M620", "T0", "G92", " E"})
        CHECK(result.gcode.find(forbidden) == std::string::npos);
    // The job must end with heaters explicitly off (the firmware preheats on
    // its own and never cools down unless commanded).
    CHECK(contains_line(result.gcode, "M104 S0"));
    CHECK(contains_line(result.gcode, "M140 S0"));
    // Presentation: pen high (pen_up 10 + 60), bed slid to max Y so the
    // sheet faces the user.
    CHECK(contains_line(result.gcode, "G1 Z70 F4800"));
    CHECK(contains_line(result.gcode, "G1 Y140 F4800"));

    // The generated file must pass the strict validator.
    const ValidationResult validation = PlotterSafetyValidator::validate(result.gcode, profile);
    INFO(validation.summary());
    CHECK(validation.ok);
}

TEST_CASE("Plotter: G-code generator rejects out-of-bounds jobs", "[Plotter]")
{
    const PlotterToolProfile profile = test_profile();
    // Square reaching machine X = 30 + 150 = 180 > max_x = 140.
    const GCodeGenResult result = PlotterGCodeGenerator::generate(
        PlotterGCodeGenerator::test_square(150.), profile);
    CHECK_FALSE(result.ok);
    CHECK(result.gcode.empty());
    CHECK(result.error.find("calibrated plotting area") != std::string::npos);
}

TEST_CASE("Plotter: G-code generator refuses invalid profiles", "[Plotter]")
{
    PlotterToolProfile profile = test_profile();
    profile.calibrated = false;
    const GCodeGenResult result = PlotterGCodeGenerator::generate(
        PlotterGCodeGenerator::test_square(), profile);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("not been calibrated") != std::string::npos);
}

TEST_CASE("Plotter: validator accepts a known-good file", "[Plotter]")
{
    const PlotterToolProfile profile = test_profile();
    const std::string good =
        "; hand-written job\n"
        "M400\n"
        "G90\n"
        "G1 Z10 F600\n"
        "G1 X30 Y30 F4800\n"
        "G1 Z2.5 F600\n"
        "G1 X50 Y30 F1800\n"
        "G4 S1\n"
        "G1 Z10 F600\n"
        "M400\n"
        "M104 S0\n"
        "M140 S0\n";
    const ValidationResult result = PlotterSafetyValidator::validate(good, profile);
    INFO(result.summary());
    CHECK(result.ok);
}

TEST_CASE("Plotter: validator rejects every prohibited category", "[Plotter]")
{
    const PlotterToolProfile profile = test_profile();
    const std::string prologue = "G90\nG1 Z10 F600\nG1 X30 Y30 F4800\n";
    const std::string epilogue = "G1 Z10 F600\n";

    auto rejects = [&](const std::string &line, const std::string &reason_fragment) {
        const ValidationResult result =
            PlotterSafetyValidator::validate(prologue + line + "\n" + epilogue, profile);
        INFO("line: " << line << "\n" << result.summary());
        CHECK_FALSE(result.ok);
        const bool found = std::any_of(result.issues.begin(), result.issues.end(),
                                       [&](const ValidationIssue &issue) {
                                           return issue.reason.find(reason_fragment) != std::string::npos;
                                       });
        CHECK(found);
    };

    // Extrusion / E axis.
    rejects("G1 X40 Y40 E1.5 F1800", "E-axis");
    rejects("M83", "E-axis");
    rejects("G92 E0", "coordinate system");
    // Heating (only the exact heater-OFF forms M104 S0 / M140 S0 are legal).
    rejects("M104 S220", "nozzle heating");
    rejects("M104 S1", "nozzle heating");
    rejects("M104 S0 T1", "nozzle heating");
    rejects("M109 S220", "nozzle heating");
    rejects("M140 S60", "bed heating");
    rejects("M140 S0.5", "bed heating");
    rejects("M190 S60", "bed heating");
    // Filament / AMS.
    rejects("M620 S0", "AMS");
    rejects("M701", "filament loading");
    rejects("M702", "filament unloading");
    rejects("M600", "filament change");
    // Tool change.
    rejects("T0", "tool changes");
    rejects("T1000", "tool changes");
    // Calibration / internal.
    rejects("M971 S11 C10", "calibration");
    rejects("M1002 judge_flag", "judge");
    rejects("G29", "bed leveling");
    // Homing inside a job (default profile).
    rejects("G28", "crush");
    // Relative mode.
    rejects("G91", "relative positioning");
    // Motion limits.
    rejects("G1 X10 Y30 F1800", "X target outside");
    rejects("G1 X30 Y200 F1800", "Y target outside");
    rejects("G1 Z1.0 F600", "below the calibrated safe pen-down");
    // Z up to present_z (pen_up 10 + 60 = 70) is allowed for the end-of-job
    // presentation move; anything above stays rejected.
    rejects("G1 Z90 F600", "above the presentation height");
    // Unknown commands stay denied.
    rejects("M42 P1 S255", "not on the plotter allowlist");
    rejects("M106 S255", "fan");
}

TEST_CASE("Plotter: validator enforces file-level rules", "[Plotter]")
{
    const PlotterToolProfile profile = test_profile();

    SECTION("empty file") {
        const ValidationResult r = PlotterSafetyValidator::validate("; nothing\n", profile);
        CHECK_FALSE(r.ok);
    }
    SECTION("XY motion before Z is established") {
        const ValidationResult r =
            PlotterSafetyValidator::validate("G90\nG1 X30 Y30 F4800\nG1 Z10 F600\n", profile);
        CHECK_FALSE(r.ok);
    }
    SECTION("motion before G90") {
        const ValidationResult r =
            PlotterSafetyValidator::validate("G1 Z10 F600\nG90\nG1 Z10 F600\n", profile);
        CHECK_FALSE(r.ok);
    }
    SECTION("must end with the pen raised") {
        const ValidationResult r = PlotterSafetyValidator::validate(
            "G90\nG1 Z10 F600\nG1 X30 Y30 F4800\nG1 Z2.5 F600\n", profile);
        CHECK_FALSE(r.ok);
        const bool found = std::any_of(r.issues.begin(), r.issues.end(), [](const ValidationIssue &i) {
            return i.reason.find("end with the pen raised") != std::string::npos;
        });
        CHECK(found);
    }
    SECTION("motion without feedrate") {
        const ValidationResult r =
            PlotterSafetyValidator::validate("G90\nG1 Z10\nG1 Z10 F600\n", profile);
        CHECK_FALSE(r.ok);
    }
    SECTION("uncalibrated profile is refused") {
        PlotterToolProfile p = test_profile();
        p.calibrated = false;
        const ValidationResult r = PlotterSafetyValidator::validate("G90\nG1 Z10 F600\n", p);
        CHECK_FALSE(r.ok);
    }
}

TEST_CASE("Plotter: validator allows a single leading G28 when opted in", "[Plotter]")
{
    PlotterToolProfile profile = test_profile();
    profile.allow_homing_in_job = true;

    const std::string good =
        "G28\nG90\nG1 Z10 F600\nG1 X30 Y30 F4800\nG1 Z10 F600\n";
    CHECK(PlotterSafetyValidator::validate(good, profile).ok);

    // Still rejected after motion, with parameters, or twice.
    CHECK_FALSE(PlotterSafetyValidator::validate(
        "G90\nG1 Z10 F600\nG28\nG1 Z10 F600\n", profile).ok);
    CHECK_FALSE(PlotterSafetyValidator::validate(
        "G28 X\nG90\nG1 Z10 F600\nG1 X30 Y30 F4800\nG1 Z10 F600\n", profile).ok);
}

TEST_CASE("Plotter: job builder produces a valid uploadable container", "[Plotter]")
{
    const std::string resources_dir = std::string(TEST_DATA_DIR) + "/../../resources/plotter";
    const PlotterToolProfile profile = test_profile();

    const PlotterJob job = PlotterJobBuilder::build(
        PlotterGCodeGenerator::test_square(20., Vec2d(10., 10.)), profile,
        "unit test square", resources_dir);
    INFO(job.error);
    REQUIRE(job.ok);
    CHECK(job.file_name == "unit_test_square.gcode.3mf");
    CHECK(job.estimated_seconds > 0);

    // Plate G-code structure the firmware requires (hardware-verified).
    for (const std::string &marker :
         {"; HEADER_BLOCK_START", "; total layer number: 2", "; CONFIG_BLOCK_START",
          "; EXECUTABLE_BLOCK_START", "M73 P100 R0", "; EXECUTABLE_BLOCK_END",
          "M104 S0", "M140 S0"})
        CHECK(job.plate_gcode.find(marker) != std::string::npos);
    // The movement core is embedded verbatim (machine coords 40,40 for
    // paper 10,10 with origin 30,30).
    CHECK(job.plate_gcode.find("G1 X40 Y40") != std::string::npos);
    // Config gcode keys are neutralized — no heating anywhere.
    for (const std::string &forbidden : {"M109", "M190", "M104 S170", "M620"})
        CHECK(job.plate_gcode.find(forbidden) == std::string::npos);

    // Container: readable zip whose plate gcode and md5 entries match.
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_reader_init_mem(&zip, job.container.data(), job.container.size(), 0));
    auto extract = [&](const char *name) {
        size_t size = 0;
        void  *p    = mz_zip_reader_extract_file_to_heap(&zip, name, &size, 0);
        REQUIRE(p != nullptr);
        std::string out(static_cast<const char *>(p), size);
        mz_free(p);
        return out;
    };
    CHECK(extract("Metadata/plate_1.gcode") == job.plate_gcode);
    const std::string md5 = extract("Metadata/plate_1.gcode.md5");
    CHECK(md5.size() == 32);
    CHECK(md5.find_first_not_of("0123456789ABCDEF") == std::string::npos);
    // Template metadata set is preserved.
    for (const char *entry : {"Metadata/slice_info.config", "Metadata/plate_1.json",
                              "Metadata/project_settings.config", "[Content_Types].xml"}) {
        size_t size = 0;
        void  *p    = mz_zip_reader_extract_file_to_heap(&zip, entry, &size, 0);
        CHECK(p != nullptr);
        if (p) mz_free(p);
    }
    mz_zip_reader_end(&zip);
}

TEST_CASE("Plotter: job builder refuses invalid input", "[Plotter]")
{
    const std::string resources_dir = std::string(TEST_DATA_DIR) + "/../../resources/plotter";
    PlotterToolProfile profile = test_profile();

    SECTION("uncalibrated profile") {
        profile.calibrated = false;
        const PlotterJob job = PlotterJobBuilder::build(
            PlotterGCodeGenerator::test_square(), profile, "x", resources_dir);
        CHECK_FALSE(job.ok);
        CHECK(job.container.empty());
    }
    SECTION("out-of-bounds paths") {
        const PlotterJob job = PlotterJobBuilder::build(
            PlotterGCodeGenerator::test_square(500.), profile, "x", resources_dir);
        CHECK_FALSE(job.ok);
        CHECK(job.error.find("calibrated plotting area") != std::string::npos);
    }
    SECTION("missing resources") {
        const PlotterJob job = PlotterJobBuilder::build(
            PlotterGCodeGenerator::test_square(), profile, "x", "/nonexistent/dir");
        CHECK_FALSE(job.ok);
        CHECK(job.error.find("missing resource") != std::string::npos);
    }
}

TEST_CASE("Plotter: project v2 round-trip and placement math", "[Plotter]")
{
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">
        <polyline points="0,0 20,0 20,20" fill="none" stroke="black" stroke-width="1"/>
    </svg>)";

    PlotterProject p;
    ProjectArtwork a;
    a.name            = "corner";
    a.info.svg_markup = svg;
    a.info.doc_width = 100.; a.info.doc_height = 100.;
    a.info.pivot = Vec2d(10., 90.);          // ink bbox center of the polyline
    a.placement.offset     = Vec2d(90., 90.);
    a.placement.rotation_z = M_PI / 2.;      // 90 deg CCW
    a.placement.scale      = 2.;
    p.artworks.push_back(a);

    // Raw import: first point at (0,100) after Y-flip; pivot-local (-10,10);
    // scaled (-20,20); rotated 90 CCW -> (-20,-20); shifted -> (70,70).
    const PlotPaths placed = p.artworks.front().placed_paths_bed();
    REQUIRE(placed.size() == 1);
    CHECK((placed.front().points.front() - Vec2d(70., 70.)).norm() < 1e-3);

    // JSON round-trip preserves everything.
    PlotterProject q;
    std::string err;
    REQUIRE(q.deserialize_json(p.serialize_json(), &err));
    CHECK(err.empty());
    REQUIRE(q.artworks.size() == 1);
    const ProjectArtwork &b = q.artworks.front();
    CHECK(b.name == "corner");
    CHECK(b.info.svg_markup == svg);
    CHECK((b.info.pivot - a.info.pivot).norm() < 1e-9);
    CHECK((b.placement.offset - a.placement.offset).norm() < 1e-9);
    CHECK(b.placement.rotation_z == Approx(a.placement.rotation_z));
    CHECK(b.placement.scale == Approx(2.));

    // Old/unknown versions are refused, not misread.
    PlotterProject old_version;
    CHECK_FALSE(old_version.deserialize_json(R"({"version":1,"artworks":[]})", &err));
    CHECK(err.find("version") != std::string::npos);
}

TEST_CASE("Plotter: artwork mesh is a warning-free triangle soup", "[Plotter]")
{
    PlotPath open_path;
    open_path.points = {Vec2d(0., 0.), Vec2d(10., 0.), Vec2d(10., 10.)};
    PlotPath closed_square;
    closed_square.closed = true;
    closed_square.points = {Vec2d(20., 0.), Vec2d(30., 0.), Vec2d(30., 10.), Vec2d(20., 10.)};
    PlotPath degenerate;
    degenerate.points = {Vec2d(50., 50.), Vec2d(50., 50.)}; // zero-length segment
    const PlotPaths paths = {open_path, closed_square, degenerate};

    const Vec2d pivot(15., 5.);
    const indexed_triangle_set its = artwork_mesh(paths, pivot);

    // 2 open segments + 4 closed-square segments, 12 triangles each; the
    // degenerate segment is skipped.
    REQUIRE(its.indices.size() == 6 * 12);
    REQUIRE(its.vertices.size() == 6 * 8);

    // Closed disconnected shells: no open edges, nothing for mesh repair to
    // flag in the object list.
    CHECK(its_num_open_edges(its) == 0);
    TriangleMesh mesh(its);
    CHECK(mesh.stats().manifold());
    CHECK_FALSE(mesh.stats().repaired());

    // Pivot is baked in: the mesh is centered on (0,0).
    const BoundingBoxf3 bb = mesh.bounding_box();
    CHECK(bb.center().x() == Approx(0.).margin(0.5));
    CHECK(bb.min.z() == Approx(0.));
    CHECK(bb.max.z() == Approx(0.2));
}

TEST_CASE("Plotter: artwork mesh decimates very dense paths", "[Plotter]")
{
    // A straight line sampled at 60k points collapses to almost nothing
    // under Douglas-Peucker; the mesh must respect the segment cap.
    PlotPath dense;
    dense.points.reserve(60001);
    for (int i = 0; i <= 60000; ++i)
        dense.points.emplace_back(i * 0.001, 0.);
    ArtworkMeshParams params;
    params.max_segments = 20000;
    const indexed_triangle_set its = artwork_mesh({dense}, Vec2d(30., 0.), params);
    CHECK(its.indices.size() <= params.max_segments * 12);
    CHECK(!its.indices.empty());
}

TEST_CASE("Plotter: preview G-code variant renders like a print but is never sendable", "[Plotter]")
{
    const PlotterToolProfile profile = test_profile();
    const PlotPaths square = PlotterGCodeGenerator::test_square(20., Vec2d(10., 10.));

    const GCodeGenResult send    = PlotterGCodeGenerator::generate(square, profile);
    const GCodeGenResult preview = PlotterGCodeGenerator::generate_preview(square, profile);
    REQUIRE(send.ok);
    REQUIRE(preview.ok);

    // Parser hooks: producer line, config block, role/width/height tags,
    // relative extrusion.
    CHECK(preview.gcode.rfind("; BambuStudio", 0) == 0);
    CHECK(contains_line(preview.gcode, "; CONFIG_BLOCK_START"));
    CHECK(contains_line(preview.gcode, "; CONFIG_BLOCK_END"));
    CHECK(contains_line(preview.gcode, "; FEATURE: Outer wall"));
    CHECK(contains_line(preview.gcode, "; LINE_WIDTH: 0.5"));
    CHECK(contains_line(preview.gcode, "M83"));

    // Same motion, shifted by the pen offset: paper (10,10) -> machine (40,40)
    // in the sendable job, pen-tip bed (75,40) in the preview (pen_offset 35,0).
    CHECK(contains_line(send.gcode, "G1 X40 Y40 F4800"));
    CHECK(contains_line(preview.gcode, "G1 X75 Y40 F4800"));
    // Draw moves carry synthetic E; identical draw length accounting.
    CHECK(contains_line(preview.gcode, "G1 X95 Y40 E0.8 F1800"));
    CHECK(preview.draw_length == Approx(send.draw_length));
    CHECK(preview.travel_length == Approx(send.travel_length));

    // The safety validator must reject the preview variant outright.
    const ValidationResult validation = PlotterSafetyValidator::validate(preview.gcode, profile);
    CHECK_FALSE(validation.ok);
}

TEST_CASE("Plotter: SVG import reports fill regions", "[Plotter]")
{
    // A filled ring (outer square with a square hole, evenodd) plus one
    // stroked open polyline.
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">
        <path fill="black" fill-rule="evenodd" d="M10,10 L50,10 L50,50 L10,50 Z M25,25 L35,25 L35,35 L25,35 Z"/>
        <polyline points="60,60 90,60 90,90" fill="none" stroke="black" stroke-width="1"/>
    </svg>)";

    const SvgImportResult r = SvgPlotImporter::import_memory(svg);
    REQUIRE(r.ok);
    // Outlines: 2 fill contours + 1 stroked polyline.
    CHECK(r.paths.size() == 3);
    REQUIRE(r.fill_regions.size() == 1);
    const SvgFillRegion &region = r.fill_regions.front();
    CHECK(region.even_odd);
    REQUIRE(region.contours.size() == 2);
    for (const PlotPath &c : region.contours)
        CHECK(c.closed);
}

TEST_CASE("Plotter: line hatching fills areas and respects holes", "[Plotter]")
{
    // 20x20 square with a 8x8 hole in the middle (evenodd).
    SvgFillRegion region;
    region.even_odd = true;
    PlotPath outer, hole;
    outer.closed = hole.closed = true;
    outer.points = {Vec2d(0., 0.), Vec2d(20., 0.), Vec2d(20., 20.), Vec2d(0., 20.)};
    hole.points  = {Vec2d(6., 6.), Vec2d(14., 6.), Vec2d(14., 14.), Vec2d(6., 14.)};
    region.contours = {outer, hole};

    HatchParams params;
    params.pattern   = HatchPattern::Lines;
    params.spacing   = 1.0;
    params.angle_deg = 0.; // horizontal lines

    const PlotPaths hatch = hatch_fill_regions({region}, params);
    REQUIRE(!hatch.empty());
    // Serpentine chaining: a handful of continuous zigzag strokes, not one
    // stab per scanline (that would be ~19+ paths and as many pen lifts).
    CHECK(hatch.size() <= 12);

    double total = 0.;
    for (const PlotPath &p : hatch) {
        REQUIRE(p.points.size() >= 2);
        CHECK_FALSE(p.closed);
        total += p.length();
        for (const Vec2d &pt : p.points) {
            // Inside the outer square...
            CHECK(pt.x() >= -0.01); CHECK(pt.x() <= 20.01);
            CHECK(pt.y() >= -0.01); CHECK(pt.y() <= 20.01);
        }
        // ...and no drawn segment (scanline or connector) runs through the
        // interior of the hole.
        for (size_t i = 1; i < p.points.size(); ++i) {
            const Vec2d mid = (p.points[i - 1] + p.points[i]) * 0.5;
            const bool inside_hole = mid.x() > 6.3 && mid.x() < 13.7 && mid.y() > 6.3 && mid.y() < 13.7;
            CHECK_FALSE(inside_hole);
        }
    }
    // Coverage sanity: 20x20 minus 8x8 at 1mm spacing is ~336mm2 -> ~336mm
    // of hatch lines plus short serpentine connectors.
    CHECK(total > 250.);
    CHECK(total < 450.);
}

TEST_CASE("Plotter: concentric hatching emits closed shrinking rings", "[Plotter]")
{
    SvgFillRegion region;
    PlotPath square;
    square.closed = true;
    square.points = {Vec2d(0., 0.), Vec2d(20., 0.), Vec2d(20., 20.), Vec2d(0., 20.)};
    region.contours = {square};

    HatchParams params;
    params.pattern = HatchPattern::Concentric;
    params.spacing = 1.0;

    const PlotPaths rings = hatch_fill_regions({region}, params);
    // 20mm square at 1mm inset per generation -> ~9 rings.
    REQUIRE(rings.size() >= 7);
    CHECK(rings.size() <= 11);
    double prev_len = std::numeric_limits<double>::max();
    for (const PlotPath &ring : rings) {
        CHECK(ring.closed);
        CHECK(ring.length() < prev_len + 0.01);
        prev_len = ring.length();
    }
}

TEST_CASE("Plotter: cold 20mm square end-to-end (software half)", "[Plotter]")
{
    // Implementation-order step 12, software portion: the hard-coded square
    // must generate, validate, and stay strictly inside the calibrated area.
    const PlotterToolProfile profile = test_profile();
    const PlotPaths square = PlotterGCodeGenerator::test_square(20., Vec2d(5., 5.));
    const PlotPaths ordered = PathOptimizer::optimize(square, Vec2d(0., 0.));
    const GCodeGenResult gen = PlotterGCodeGenerator::generate(ordered, profile);
    REQUIRE(gen.ok);
    const ValidationResult validation = PlotterSafetyValidator::validate(gen.gcode, profile);
    INFO(validation.summary());
    REQUIRE(validation.ok);
    CHECK(gen.draw_length == Approx(80.));
}
