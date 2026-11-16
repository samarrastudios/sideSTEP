<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/brand/github/readme-header-dark.svg">
  <img src="assets/brand/github/readme-header-light.svg" alt="sideSTEP: STEP files, seen without the CAD package." width="420">
</picture>

# sideSTEP

See STEP files without opening a CAD package: thumbnails and a spinnable preview in File Explorer, and a lightweight 3D viewer that hands the file to your CAD app when you need more.

Open source, for Windows. Created by Adrian Samarra.

## Status

| Piece | State |
|---|---|
| STEP loading, assembly colors, tessellation (`core/`) | Working on Windows (MSVC 2022, OpenCASCADE 8.0.1) and on Linux (OpenCASCADE 7.6) |
| Software thumbnail renderer (`core/renderer.cpp`) | Working |
| Command-line tester (`cli/`) | Working |
| Explorer thumbnail DLL (`shellext/`) | Working: thumbnails confirmed in File Explorer on real assemblies. **Loses to "STEP Explorer Preview" if that Store app is installed** (see Known limits) |
| Explorer preview pane (`shellext/preview_handler.cpp`) | Working through Windows' preview host: drag to spin, scroll to zoom, right-drag to pan |
| Viewer (`viewer/`, Qt 6 + OpenCASCADE 3D view) | Working: cascading part tree, floating tool bar, view cube, readout strip, display styles, measure, section, section box, explode, parts list, snapshot, full screen, next/previous file, "Open in" another app |
| Setup program (`installer/sideSTEP.iss`, Inno Setup) | Working: per-user install and uninstall tested on the development machine. Not code-signed |

## Build (Windows)

Needs Visual Studio 2022 or newer (or Build Tools) with **Desktop development with C++**, and Git.

```powershell
scripts\build.ps1      # first run builds OpenCASCADE and Qt: 1-2 hours, once
scripts\test.ps1       # renders samples\*.step to PNGs and opens the folder
scripts\register.ps1   # turns on thumbnails and the preview pane from the build folder (per-user, no admin)
scripts\unregister.ps1 # turns them off again
scripts\install.ps1    # installs the viewer and the Explorer integration for this user under %LOCALAPPDATA%\Programs\sideSTEP, listed in Settings > Apps
scripts\uninstall.ps1  # removes that install
scripts\package.ps1    # builds, then makes build\installer\sideSTEP-Setup-<version>.exe (needs Inno Setup 6)
```

The setup program installs for the current user without admin rights, ships the Visual C++ runtime files beside the app, and has one checkbox: whether sideSTEP may remove Windows' downloaded-from-the-internet mark from STEP files so Explorer will preview them. Use either the setup program or `install.ps1` on a machine, not both: they install to the same folder but keep separate entries under Installed apps.

Thumbnails not appearing? Set the string value `DebugLog` under `HKCU\Software\sideSTEP` to a file path; the DLL then appends one line per step (load, initialize, result code) to that file. If the file stays empty, Windows is not calling sideSTEP at all, which usually means another handler has priority.

If PowerShell refuses to run scripts: `powershell -ExecutionPolicy Bypass -File scripts\build.ps1`.

## Using it

- **In Explorer:** STEP files show thumbnails. Turn on the preview pane (View > Preview pane, or Alt+P) and select a file: drag in the pane to spin the model, scroll to zoom, right-drag to pan, double-click to reset. For files downloaded from the internet, Windows shows "The file you are attempting to preview could harm your computer" in place of any preview handler. Once installed, sideSTEP prevents that for STEP files: a small background helper (`sideSTEP.exe --watch`, started at sign-in) removes Windows' downloaded-from-the-internet mark from `.step`, `.stp` and `.p21` files, and from no other file type. It is the user's choice: `scripts\install.ps1 -NoPreviewHelper` installs without it, and File > "Preview downloaded STEP files in Explorer without a warning" in the viewer switches it on or off at any time.
- **Viewer:** `sideSTEP.exe file.step`, or drop a file on the window. Left-drag orbits, right- or middle-drag pans, the wheel zooms. Page Up and Page Down step through the STEP files in the same folder.
- **Make it the default:** after `scripts\install.ps1`, right-click a STEP file > Open with > Choose another app > sideSTEP > Always. Windows only lets the user make that choice; File > "Make sideSTEP the default for STEP files" in the viewer opens the right Settings page.
- **Hand off to CAD:** File > Open with lists the other programs registered for STEP files, or browse to any exe. The one you pick becomes the "Open in ..." button (Ctrl+E), so sideSTEP is the quick look and the CAD package is one click away.

- **Layout:** the part tree is docked on the left and cascades as the file has it; an eye on each row hides or shows the part, groups show how many parts they hold, and the box above searches. A floating bar sits over the bottom of the model: Fit, Display | Measure, Section, Section box, Explode | Parts, Snapshot, Full screen. The active tool's settings float in a pill at the top. The view cube (top right) turns the camera to a face, edge or corner; the house under it goes home. The strip under the model reads out the whole model's size and part count, or the selected part's name, place in the tree and size.
- **One tool at a time.** Picking another tool clears the first one's effect. Esc backs out one step per press: a half-done measurement, then the tool, then the selection, then full screen.
- **Measure (M):** Distance (two picks: shortest distance, X/Y/Z deltas, angle, hole centre-to-centre), Point to point, Length / diameter (one click), Angle, Overall size. Measurements stay in a list; Copy puts them on the clipboard.
- **Section (S)** cuts across X, Y or Z. **Section box (B)** keeps what is inside a box. Cut faces are filled in the part's colour.
- **Display:** Shaded with edges (E), Shaded, See-through (T), Wireframe (W).
- **Show only (I)** fades everything except the selected parts; **Hide (H)** hides them; Shift+H shows everything.
- **Parts (P)** lists every part with its count and copies as a list. **Snapshot (C)** saves a picture to Pictures\sideSTEP and copies it.
- **Sizes** are shown in the file's own units first (inches or millimetres), then the other.
- **Explode (X)** moves each item away from the middle of the assembly it belongs to, so sub-assemblies separate as groups and their parts spread around the group. All, X, Y or Z chooses the direction.
- **Mouse:** the switcher at the right of the readout strip (also View > Mouse) offers the mappings of SolidWorks, Fusion / Inventor, Onshape, Creo / NX and Rhino as well as sideSTEP's own, with a separate switch for the scroll wheel's zoom direction. Double-click zooms to a spot.
- **Look:** a main light from above casts soft shadows (Display > Shadows); Display > Perspective (O) switches from the flat CAD projection to a perspective view.

| Key | Action |
|---|---|
| F | Fit |
| Ctrl+1 to Ctrl+7 (or Shift+1 to Shift+7) | Front, back, left, right, top, bottom, isometric |
| E, T, W, O | Shaded with edges, see-through, wireframe; perspective on or off |
| M, S, B, X | Measure, section, section box, explode |
| P, C, Shift+F | Parts, snapshot, full screen |
| H, I, Shift+H | Hide, show only, show all |
| Esc | Back out one step |

## Layout

- `core/` - portable C++: STEP bytes in, pixels out. No Windows code. Shared by everything.
- `shellext/` - the COM DLL Explorer loads: thumbnails (`IThumbnailProvider`) and the preview pane (`IPreviewHandler`, drawn with plain OpenGL 1.1, no Qt).
- `viewer/` - the Qt 6 app. `model.cpp` loads the STEP file into an XCAF document on a worker thread, `occ_view.cpp` hosts OpenCASCADE's 3D view, `main_window.cpp` is the UI, `hud.cpp` is the floating bar, pill and toast, `view_cube.cpp` the view cube, `measure_tool.cpp` and `section_tool.cpp` the tools, `icons.cpp` draws the icons, `file_assoc.cpp` registers the file type (`sideSTEP.exe --register`).
- `tools/preview_test.cpp` - `sidestep-previewtest`, drives the preview handler off-screen and saves what it drew.
- `cli/` - `sidestep-thumb in.step out.png [size]`, prints timings.
- `tools/make_sample.cpp` - generates `samples/sample.step`.
- `samples/` - small test files.

## How the thumbnail works

1. Explorer starts an isolated `dllhost.exe`, loads the DLL and passes the file as a stream.
2. OpenCASCADE parses the STEP file into an assembly document (parts, placements, colors).
3. Each distinct part is tessellated once, with a tolerance sized to the whole model, so small fasteners stay cheap.
4. A CPU z-buffer renderer draws an isometric view with edge lines, anti-aliased, on a transparent background. No GPU, because thumbnail hosts often don't have one.

## Known limits

- OpenCASCADE's STEP import is the slow part. A 15 MB, 211-part assembly took about 16 s on a 2-core test machine (12 s of it in OCCT's transfer step). Explorer renders thumbnails in the background and caches them, so this is a one-time cost per file, but it wants tuning.
- If the Store app "STEP Explorer Preview" is installed, Windows uses its thumbnail handler for `.step`, `.stp` and `.p21` instead of sideSTEP's, because handlers from packaged apps take priority over classic registry ones. Uninstall that app to get sideSTEP thumbnails. `scripts\register.ps1` warns when it finds it.
- The sample files take about 0.1 s each to thumbnail (0.2 s through Explorer, including host start-up).
- Thumbnails are meshed on one thread. OpenCASCADE's parallel mesher leaves a thread pool behind that hangs the host process at exit when used from a DLL.
- Files over 400 MB are skipped.
- No up-axis detection: Z is assumed up.
- The viewer and the preview pane load the whole file before showing anything. On the development machine a 22 MB, 679-part assembly takes about 10 s in the viewer (with a progress bar); the preview pane shows "Loading preview..." meanwhile and loads on one thread, so it is slower.
- The viewer draws no text inside the 3D view (OpenCASCADE is built without FreeType); the measurement label is a Qt overlay.
- Explode moves every part straight out from the centre of the assembly; it does not follow the assembly hierarchy.
- Another program's preview handler from a packaged (Store) app may take priority over sideSTEP's, as with thumbnails.
- OpenCASCADE is LGPL 2.1 (with an exception). Qt is LGPL 3. This build links both statically for self-contained binaries, which is fine for personal use; before distributing, either switch to the dynamic triplet (`x64-windows`) or make the object files available, per the LGPL.

## License

sideSTEP's own code is released under the [MIT License](LICENSE). Copyright (c) 2026 Adrian Samarra.

It builds on [OpenCASCADE Technology](https://dev.opencascade.org/), which is licensed separately under the LGPL 2.1 with an exception. The wordmark uses JetBrains Mono (SIL Open Font License 1.1), converted to outlines.
