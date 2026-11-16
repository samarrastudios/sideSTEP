<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/brand/github/readme-header-dark.svg">
  <img src="assets/brand/github/readme-header-light.svg" alt="sideSTEP: STEP files, seen without the CAD package." width="420">
</picture>

# sideSTEP

See STEP files without opening a CAD package.

- **In File Explorer:** real thumbnails for `.step`, `.stp` and `.p21` files, and a preview pane where you can drag the model around.
- **A quick 3D viewer:** opens in a moment, with a part tree, measuring, section cuts and explode, and hands the file to your CAD program when you need more.

Open source, for Windows 10 and 11 (64-bit). Created by Adrian Samarra. This is an early preview (version 0.2.0).

## Install

There is no published download yet. Build the setup program from source (see [Build](#build)); it produces `build\installer\sideSTEP-Setup-<version>.exe`.

The setup program:

- installs for the current user only, with no administrator prompt;
- is not code-signed, so Windows SmartScreen warns the first time it runs (More info > Run anyway);
- has one checkbox, described under [Previewing downloaded files](#previewing-downloaded-files).

To open STEP files in sideSTEP by default, right-click a `.step` file > Open with > Choose another app > sideSTEP > Always. Windows only lets the user make that choice. Do the same once for `.stp`.

To remove it: Settings > Apps > Installed apps > sideSTEP > Uninstall.

## Using it

### In File Explorer

STEP files show thumbnails. Turn on the preview pane (View > Preview pane, or Alt+P) and select a file: drag in the pane to spin the model, scroll to zoom, right-drag to pan, double-click to reset.

### Previewing downloaded files

Windows marks files downloaded from the internet, and then refuses to show them in the preview pane ("The file you are attempting to preview could harm your computer"). There is no Windows setting for the preview pane alone.

If you leave the setup program's checkbox ticked, a small background helper (`sideSTEP.exe --watch`, started at sign-in) removes that mark from `.step`, `.stp` and `.p21` files, and from no other file type. Switch it on or off at any time in the viewer: File > "Preview downloaded STEP files in Explorer without a warning".

### The viewer

Open a file by double-clicking it, with `sideSTEP.exe file.step`, or by dropping it on the window.

- **Part tree** (left): cascades as the file has it. The eye on each row hides or shows a part, groups show how many parts they hold, and the box above searches.
- **Floating bar** (bottom of the model): Fit, Display | Measure, Section, Section box, Explode | Parts, Snapshot, Full screen.
- **Tool settings** float in a pill at the top while a tool is on. One tool is on at a time; picking another clears the first one's effect.
- **View cube** (top right): click a face, edge or corner to look from there. The house returns to the home view.
- **Readout strip** (under the model): the whole model's size and part count, or the selected part's name, place in the tree and size. Sizes are in the file's own units first, then the other system.

| Tool | What it does |
|---|---|
| Measure (M) | Distance (shortest distance, X/Y/Z deltas, angle, hole centre-to-centre), Point to point, Length / diameter, Angle, Overall size. Measurements stay in a list; Copy puts them on the clipboard. |
| Section (S) | Cuts across X, Y or Z. Cut faces are filled in the part's colour. |
| Section box (B) | Keeps only what is inside a box. |
| Explode (X) | Moves each item away from the middle of the assembly it belongs to, so sub-assemblies separate as groups. All, X, Y or Z sets the direction. |
| Parts (P) | Every part with its count; copies as a list. |
| Snapshot (C) | Saves a picture to Pictures\sideSTEP and copies it. |
| Show only (I), Hide (H) | Fade everything but the selection, or hide it. Shift+H shows everything. |
| Display | Shaded with edges (E), Shaded, See-through (T), Wireframe (W); Perspective (O); Shadows. |

Other keys: F fits the view; Ctrl+1 to Ctrl+7 (or Shift+1 to Shift+7) give front, back, left, right, top, bottom and isometric; Shift+F is full screen; Page Up and Page Down step through the STEP files in the same folder; Esc backs out one step per press (a half-done measurement, then the tool, then the selection, then full screen).

**Mouse.** By default, left-drag turns the model, right- or middle-drag pans, and the wheel zooms; double-click zooms to a spot. The switcher at the right of the readout strip (also View > Mouse) offers the mappings of SolidWorks, Fusion / Inventor, Onshape, Creo / NX and Rhino, with a separate switch for the wheel's zoom direction.

**Hand off to CAD.** File > Open with lists the other programs that open STEP files, or browse to any exe. The one you pick becomes the "Open in ..." button (Ctrl+E).

## Build

Needs Visual Studio 2022 or newer (or Build Tools) with **Desktop development with C++**, and Git. Packaging also needs Inno Setup 6 (`winget install JRSoftware.InnoSetup`).

```powershell
scripts\build.ps1      # first run builds OpenCASCADE and Qt: about two hours, once
scripts\test.ps1       # renders samples\*.step to PNGs and opens the folder
scripts\package.ps1    # builds, then makes build\installer\sideSTEP-Setup-<version>.exe
```

For working on the code without the setup program:

```powershell
scripts\register.ps1   # Explorer thumbnails and preview pane straight from the build folder
scripts\unregister.ps1 # turns them off again
scripts\install.ps1    # per-user install from the build folder (add -NoPreviewHelper to leave the helper off)
scripts\uninstall.ps1  # removes that install
```

Use either the setup program or `install.ps1` on a machine, not both: they install to the same folder but keep separate entries under Installed apps.

If PowerShell refuses to run scripts: `powershell -ExecutionPolicy Bypass -File scripts\build.ps1`.

Thumbnails or previews not appearing? Set the string value `DebugLog` under `HKCU\Software\sideSTEP` to a file path; the DLL then appends one line per step to that file. If the file stays empty, Windows is not calling sideSTEP at all, which usually means another handler has priority.

## Layout

| Folder | What is in it |
|---|---|
| `core/` | Portable C++: STEP bytes in, triangles and pixels out. No Windows or Qt code. Shared by everything. |
| `shellext/` | The DLL Explorer loads: thumbnails (`thumbnail_provider.cpp`) and the preview pane (`preview_handler.cpp`, plain OpenGL 1.1, no Qt). |
| `viewer/` | The Qt 6 app. `model.cpp` loads the file on a worker thread; `occ_view.cpp` hosts OpenCASCADE's 3D view; `main_window.cpp` is the window; `hud.cpp` and `view_cube.cpp` are the floating controls; `measure_tool.cpp` and `section_tool.cpp` are the tools; `icons.cpp` draws the icons; `open_with.cpp`, `file_assoc.cpp` and `unblock.cpp` handle the hand-off, the file type and the preview helper. |
| `cli/` | `sidestep-thumb in.step out.png [size]`: renders a thumbnail and prints timings. |
| `tools/` | `preview_test.cpp` drives the preview handler off-screen; `make_sample.cpp` generates `samples/sample.step`. |
| `installer/` | The Inno Setup script. |
| `scripts/` | Build, test, package and install scripts. |
| `samples/` | Small test files. |
| `assets/brand/` | Icon, wordmark and brand rules. |
| `res/` | Icon and version resource shared by the binaries. |

## Known limits

- **Large files take a while.** OpenCASCADE's STEP import is the slow part and runs on one thread. On the development machine a 22 MB, 679-part assembly takes about 10 s to open in the viewer (with a progress bar), and its thumbnail and preview take longer. Thumbnails are cached by Explorer after the first time.
- **"STEP Explorer Preview".** If that Store app is installed, Windows uses its thumbnail handler instead of sideSTEP's, because handlers from packaged apps take priority. The same is likely for the preview pane. Uninstall that app to get sideSTEP's.
- **Explode on flat files.** A file that keeps every part directly under one item has no groups to keep together, so its parts spread straight out from the centre.
- **No text in the 3D view.** OpenCASCADE is built without FreeType, so measurement labels are drawn by Qt on top.
- **Section planes move by slider**, not by dragging in the view.
- **Z is assumed up.** There is no up-axis detection.
- Files over 400 MB get no thumbnail or preview.
- The setup program has been tested on one PC only, and is not code-signed.

## License

sideSTEP's own code is released under the [MIT License](LICENSE). Copyright (c) 2026 Adrian Samarra.

It is built with [OpenCASCADE Technology](https://dev.opencascade.org/) (LGPL 2.1 with an exception) and [Qt 6](https://www.qt.io/) (LGPL 3), both linked statically into the binaries. This repository holds the complete source and the build scripts that reproduce them, which is what you need to rebuild sideSTEP against your own copies of those libraries. The wordmark uses JetBrains Mono (SIL Open Font License 1.1), converted to outlines.
