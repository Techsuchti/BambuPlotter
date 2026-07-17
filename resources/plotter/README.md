# BambuPlotter job resources

These files are the hardware-verified skeleton for movement-only pen-plotter
jobs on the Bambu Lab A1 mini (verified on firmware v01.08.01.00, LAN-only
Developer Mode, 2026-07-18).

## Why a template exists at all

The A1 mini firmware only executes a LAN job (`project_file` MQTT command)
when the uploaded `.gcode.3mf` looks like a genuine sliced project:

- missing `; HEADER_BLOCK_*` in the plate G-code → job hangs on "Preparing"
- missing `; CONFIG_BLOCK_*` → job is silently auto-cancelled
- bare/minimal zip containers → job is accepted (`result=success`!) but never
  runs, and a wedged task then blocks all later jobs with error 0500-4004

The firmware treats the CONFIG_BLOCK as inert metadata (it does NOT execute
`machine_start_gcode` from it), but its presence is mandatory.

Two further firmware behaviors matter:

- The firmware preheats the nozzle to ~75 °C on its own when any print
  starts. This cannot be disabled by file content or command (community
  consensus + our tests); it is harmless for a side-mounted pen.
- The firmware does NOT cool down after FINISH unless the job commands it —
  every plotter job must end with `M104 S0` and `M140 S0` (the only heater
  commands the plotter safety validator permits).

## Files

- `plot_container_template.gcode.3mf` — a complete container produced by THIS
  repository's own CLI slicer (a 10 mm cube sliced for "Bambu Lab A1 mini
  0.4 nozzle" / "0.20mm Standard @BBL A1M" / "Bambu PLA Basic @BBL A1M"),
  with the plate G-code's executable block replaced by a movement-only job
  and all G-code keys in the CONFIG_BLOCK neutralized to comments.
  PlotterJobBuilder replaces `Metadata/plate_1.gcode` and
  `Metadata/plate_1.gcode.md5` inside it for each job.
- `plate_config_block.txt` — the neutralized CONFIG_BLOCK spliced into every
  generated plate G-code (extracted from the same verified plate).

## Regenerating

1. Slice any small model with this repo's binary:
   `BambuStudio --load-settings "<machine.json>;<process.json>"
   --load-filaments "<filament.json>" --slice 0 --export-3mf out.gcode.3mf cube.stl`
2. Neutralize every `*_gcode` key in the CONFIG_BLOCK (movement-only comments)
   and replace the EXECUTABLE_BLOCK with a validated plotter job.
3. Verify on hardware: upload via FTPS, start via `project_file` with
   `url: "ftp:///<name>"`, confirm RUNNING → FINISH with `mc_percent` 100 and
   `nozzle_target_temper` returning to 0 at FINISH.
