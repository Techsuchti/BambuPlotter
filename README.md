# BambuPlotter

**Turn your Bambu Lab A1 mini into a pen plotter — with the exact experience of preparing a print in Bambu Studio.**

![Printer](https://img.shields.io/badge/printer-Bambu%20Lab%20A1%20mini-6E8CA0)
![Platform](https://img.shields.io/badge/platform-macOS%20Apple%20Silicon-6E8CA0)
![Connection](https://img.shields.io/badge/connection-LAN%20Developer%20Mode-6E8CA0)
![Base](https://img.shields.io/badge/fork%20of-Bambu%20Studio%20v02.08.01.55-6E8CA0)

BambuPlotter is a fork of [Bambu Studio](https://github.com/bambulab/BambuStudio) that replaces the 3D-printing workflow with a **plotter-first** one: import an SVG, place it on the plate with the native gizmos, hit **Generate plot**, preview the pen paths as ink on paper, and **Send plot** — your printer draws it with a real pen, standalone, and hands you the sheet when it's done.

This is not an app inside another app. It *is* Bambu Studio's workflow — just for pens instead of nozzles.

- **Faithful SVG plotting** — strokes, solid fills (hatched to your pen's tip width, `Lines` or `Concentric` patterns), painter's-order color handling (light shapes erase, like the original artwork renders), and single centerline strokes for lines thinner than the pen.
- **Native experience** — artwork is a real plate object: drag, rotate, scale with the standard toolbar and gizmos. The Preview tab shows the plot like a sliced print — black ink on a white paper sheet — with time estimation and travel moves.
- **Safety by construction** — jobs are movement-only G-code (no heating, no extrusion, no homing), checked by a default-deny validator against your calibrated bounds before anything is sent.
- **Projects** — save/open `.bplot` files carrying your artwork (SVG embedded), placements, and pen/fill settings. Machine calibration never travels with a project.

> **Unofficial software.** Not affiliated with Bambu Lab. You are driving real hardware with a modified tool — supervise your first plots and use at your own risk.

---

## Step 0 — Print a pen holder

You need a way to hold a pen next to the toolhead. Recommended (this is what BambuPlotter was developed and tested with):

**[A1 Plotter Module on MakerWorld](https://makerworld.com/en/models/2433877-a1-plotter-module?from=search#profileId-2737084)** — print it on your A1 mini *before* you convert it.

Any rigid side-mount pen holder will work — BambuPlotter doesn't assume a specific geometry. The calibration wizard measures **your** setup (paper position, safe bounds, pen heights, and the pen↔nozzle offset).

**One hard rule:** if your pen tip sits *below* the nozzle tip (most holders), **never home the printer with the pen mounted** — Z-homing presses the bed against the toolhead and would crush the pen. The workflow is always: home bare → mount pen → calibrate → plot. The wizard enforces it.

## Step 1 — Printer setup

1. On the printer: enable **LAN-Only Mode** with **Developer Mode** (network settings) and note the access code.
2. BambuPlotter talks to the printer over LAN via the open-source [open-bamboo-networking](https://github.com/ClusterM/open-bamboo-networking) plugin (discovery, live status, FTPS upload, job control).
3. In the app's **Device** tab, connect to your printer with the access code.

## Step 2 — Calibrate (once per paper/pen setup)

Tape your paper to the plate, then open **Prepare → sidebar → Calibrate…** The wizard walks you through six steps:

1. **Home** — with the pen **off** the toolhead.
2. **Jog** — raise Z and position the head so you can mount the pen comfortably.
3. **Mount the pen** — tick the checkbox; homing is locked out from here on.
4. **Capture positions** — jog the pen tip to the paper's front-left corner (paper origin), then the min and max corners of the usable area; lower Z until the tip just kisses the paper (pen contact), raise to a comfortable travel height (pen up).
5. **Pen offset** *(optional but recommended)* — hover the pen tip over a small mark and capture, then hover the nozzle over the same mark and capture. This aligns the on-screen paper and artwork with where ink physically lands.
6. **Finish** — the profile is validated and saved.

The calibrated paper area appears on the plate as a white sheet. Reopening the wizard later keeps the machine position — no re-homing needed.

## Step 3 — Plot

1. **Import an SVG** (File → Import, drag & drop, or the toolbar). It lands on the paper as a real object — move, rotate, scale it like any model.
2. Tune **Pen** (tip width, speeds) and **Fill** (hatching on/off, pattern, spacing × tip width, angle) in the sidebar. Tip width drives hatch density — a 0.5 mm pen fills denser than a 0.8 mm one.
3. **Generate plot** — the plotter's "slice". The Preview shows ink on paper with stroke count, drawing/travel length, and a time estimate. Moving anything makes the plot stale until you regenerate — the printer always runs exactly what you previewed.
4. **Send plot** — confirm, and the job uploads and starts. The printer runs it standalone (you can disconnect). At the end, the pen lifts and the bed slides forward to present the sheet. Heaters are commanded off at the end of every job.

## Demos

<!-- gifs / videos go here -->

*Coming soon.*

## Limitations

- **Hardware**: developed and hardware-tested on the **A1 mini** only.
- **Platform**: builds and runs on **macOS (Apple Silicon)**; other platforms untested.
- **Firmware preheat**: the A1 firmware preheats the nozzle to ~75 °C at job start on its own — this cannot be disabled. It's harmless with a side-mounted pen, and every job explicitly turns heaters off at the end.
- **One pen per plot**: no multi-color/multi-pen passes (yet). SVG colors are interpreted as ink (dark) vs. paper (light).
- **Fine detail is physics-bound**: white gaps narrower than the pen tip can't survive on paper — use a finer pen for detailed line art.
- **Preview at far zoom**: very sparse hatching can look denser than it is (sub-pixel rendering) — zoom in for the true spacing. The plotted output is always true to the data.
- **Camera view** is unavailable (limitation of the open networking plugin).

## Building

macOS, Apple Silicon:

```bash
# dependencies (once)
CMAKE_BUILD_PARALLEL_LEVEL=10 ./BuildMac.sh -d -a arm64 -x

# app (incremental build + package)
CMAKE_BUILD_PARALLEL_LEVEL=10 ./BuildMac.sh -s -a arm64 -x -b
```

The packaged app lands at `build/arm64/BambuStudio/BambuPlotter.app`. Engine tests: build and run the `plotter_tests` target.

BambuPlotter keeps its settings in `~/Library/Application Support/BambuPlotter` — it never touches an official Bambu Studio installation on the same machine.

## Contributing

Issues, ideas, and pull requests are welcome — other printer models, other platforms, hatching patterns, multi-pen support… the fun has just started.

For anything related: **daniel.oquelis@gmail.com**

## License & credits

Licensed under the **GNU Affero General Public License v3**, like the projects it stands on: [Bambu Studio](https://github.com/bambulab/BambuStudio) by Bambu Lab, based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, itself based on [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community. LAN connectivity by [open-bamboo-networking](https://github.com/ClusterM/open-bamboo-networking).
