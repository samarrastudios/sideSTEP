# sideSTEP - project notes for Claude Code

Windows utility: STEP files get real thumbnails and a spinnable preview pane in File Explorer, and open in a lightweight native 3D viewer. The viewer is the quick look: it is meant to be the default app for STEP files and hands off to the owner's CAD package with one click. Owner: Adrian. Comparable product: "STEP Explorer Preview" on the Microsoft Store.

This file is public (the repo is on GitHub). Keep employer, client, product and file names, and private folder paths, out of it and out of everything else that is committed. Describe test files generically ("a 22 MB, 679-part assembly").

## Decisions already made
- All native C++17. Geometry kernel: OpenCASCADE via the vcpkg manifest (`vcpkg.json`), triplet `x64-windows-static-md`.
- Viewer: Qt 6 Widgets + OCCT's AIS/V3d viewer (not web tech). Qt comes from vcpkg (`qtbase`, minimal features, same triplet), so the viewer is one self-contained exe.
- `core/` stays free of Windows and Qt headers; `core/thumb_core.h` stays free of OCCT types. The shell DLL stays free of Qt: the preview pane is plain Win32 + OpenGL 1.1 on the `core/` triangle mesh.
- Viewer design: follows a hand-off brief and approved mock the owner brought from a browser 3D viewer built in another project (2026-11-16). Part tree with eyes, counts and search; floating bar of nine buttons; tool settings in a pill; view cube with home; readout strip; one tool at a time; Esc backs out one step per press; display styles as one list. Nothing is copied from that project (code, icons, strings, house style), and sideSTEP keeps its own brand. Where the brief lives is in the assistant's private memory.
- Branding: `assets/brand/` is the brand packet; read `assets/brand/BRAND.md` before making anything user-facing. Graphite window chrome, Paper 3D background by default (black parts vanish on Graphite), Signal orange as the one accent (the active tool, selection). "sideSTEP" in prose, `sidestep` in code.
- Credit line for the installer, About box and GitHub: "Created by Adrian Samarra. Open source, for Windows, released on GitHub."
- Everything installs per user (HKCU), no admin.
- Default app: the installer registers the ProgID and capabilities; Windows makes the user confirm once (UserChoice is hash-protected).
- Hand-off: "Open in <app>" launches a CAD app the user picks (File > Open with). Windows stops recommending the previous default once sideSTEP takes over, so the list also offers known CAD exes (`IsKnownCadApp` in `open_with.cpp`).
- Preview helper: Windows refuses to run any preview handler on a file carrying the downloaded-from-the-internet mark, and has no setting for the preview pane alone. `sideSTEP.exe --watch` removes the mark from .step/.stp/.p21 only. It is opt-out and the user's choice everywhere: ticked-by-default installer task, File menu switch, `--helper-on` / `--helper-off`, `install.ps1 -NoPreviewHelper`.
- Distribution: source on GitHub; a per-user Inno Setup program; no Store, no MSIX. Unsigned for now (SmartScreen warns).
- The project name lives in `CMakeLists.txt` (`APP_NAME`, `APP_DISPLAY_NAME`); the version in `CMakeLists.txt`, `vcpkg.json` and the default in `installer/sideSTEP.iss`.

## State (2026-11-16, version 0.2.0, pushed to `main`, released as the preview `v0.2.0`)
- Builds clean: Build Tools 2022 (MSVC 19.44), OpenCASCADE 8.0.1, Qt 6.11.3. First build: OCCT ~55 min, Qt ~50 min (vcpkg builds a full host copy of Qt for its tools as well as the minimal target copy). Later builds take seconds to a couple of minutes.
- **Thumbnails** (`shellext/thumbnail_provider.cpp`): confirmed in Explorer on the owner's real files.
- **Preview pane** (`shellext/preview_handler.cpp`): drag spins, wheel zooms, right-drag pans, double-click resets, mark bottom-right. Loads on a worker thread. Verified through prevhost.exe.
- **Viewer** (`viewer/`): tree, floating bar, pill, view cube, readout strip; measure (distance, point to point, length/diameter, angle, overall size; kept in a list); section and section box (OCCT clip planes with capping); grouped explode with All/X/Y/Z; display styles, shadows, perspective; parts list; snapshot; full screen; show only; six mouse styles plus a reverse-scroll switch; next/previous file; hand-off.
- **Setup program**: `scripts\package.ps1` -> `build\installer\sideSTEP-Setup-<version>.exe`. Ships msvcp140, msvcp140_1, vcruntime140 and vcruntime140_1 beside the app. Install, uninstall and reinstall verified silently on the development machine.
- Hardware exceptions inside OCCT are caught in the shell DLL (`SafeLoadStep`, `SafeRenderThumbnail`); the viewer catches only C++ exceptions.
- Typical times on the development machine: viewer 31 MB / 1541 parts ~10 s, 22 MB / 679 parts ~10 s, 6 MB ~3 s. Thumbnails of the same files 14-28 s. OCCT's transfer step dominates and is single-threaded.

Verified only by off-screen runs or not at all (the owner clicking through is the real test): eye toggles, show only, parts list, snapshot, full screen, the Esc sequence, view cube clicks, mouse styles, double-click zoom, the File menu helper switch, the setup wizard's pages, and any second PC.

## How things work, and why
- **Floating controls** (`hud.cpp`) are a separate frameless translucent window kept over the 3D view and masked to its controls, so clicks elsewhere reach the model. The 3D view is a native OpenGL window and Qt cannot composite translucent widgets over it. The alternative is rendering OCCT inside a QOpenGLWidget: needs Qt rebuilt with its `opengl` feature (~20 min) and a rewrite of `occ_view.cpp`. Worth it if the overlay ever lags or has z-order trouble.
- **No FreeType in OCCT**, so no text in the 3D view. The view cube is painted by Qt (`view_cube.cpp`), and measurement labels are Qt widgets placed over the view. Cube labels are drawn as outlines because skewed text picks up ClearType colour fringes. Rebuilding OCCT with FreeType takes ~55 min.
- **Fit** uses sampled surface points per part (`ModelNode::outline`), not bounding boxes: boxes leave L-shaped or lopsided models off to one side, and some files carry stray geometry far from the solids.
- **Explode**: the slider sets a target and a 60 Hz timer eases to it. Each item moves away from its parent assembly's middle and carries its children. While moving, parts are placed with `SetLocalTransformation` and the camera is fitted by arithmetic (`OccView::fitPointsQuick`); `ctx->SetLocation` (which also updates picking) runs once at rest. `V3d_View::FitAll` re-measures every object and cost 20-40 ms a frame on 1,541 parts.
- **Viewport size**: Qt can deliver a resize before the native window has its new size; `OccView::syncSize` re-reads the real size before every frame. The owner had seen the view stuck at thumbnail size until the window was resized; not reproduced, so the fix is unconfirmed.
- **Shell DLL meshes single-threaded**: OCCT's `OSD_ThreadPool` static destructor hangs a DLL host at exit. The viewer and CLI mesh in parallel.
- **Preview helper** (`viewer/unblock.cpp`, plain Win32, runs instead of the Qt app): watches every fixed drive, sweeps existing files on NTFS drives (user profile first), and follows open Explorer windows, unblocking the STEP files they list. The last part is what covers cloud drives: at least one cloud drive client marks every file as from the internet while presenting itself as FAT32 with no named streams. Read-only files have the flag lifted for a moment. Cloud placeholders are skipped so nothing gets downloaded.
- **Units**: OCCT converts geometry to mm. The readout shows the file's own units first; an inch file often also carries a helper millimetre entry, so any inch entry makes it an inch file.
- **Section box** starts around the whole model with the top off; a box around the middle is empty on a model whose parts all sit along two edges.
- **Lighting**: a key light fixed to the model casts shadow-mapped shadows; a weaker light rides with the camera; ambient fill. Shadows cost about 5 ms a frame on a million triangles.
- **Known conflict**: when "STEP Explorer Preview" (AelyoSoftworks.STEPExplorerPreview) is installed, its packaged thumbnail handler wins even though `AssocQueryString` resolves to ours. Expect the same for the preview pane. Only a packaged handler of our own could compete.
- Wheel-direction defaults per mouse style are from memory of those programs (only SolidWorks is reversed) and unchecked.

## Testing without a person
- Viewer: `sideSTEP.exe file.step --dump out.png [--explode 0-100] [--display 0-3] [--section x,0.5 | box] [--measure-mode 0-4] [--clicks "x,y;x,y"] [--view front|...|iso] [--perspective]` runs off-screen, writes the 3D view, `out.png.window.png` (whole window with the floating controls) and `out.png.txt` (measurements, readout, explode frame time, hand-off app list), then quits. `--clicks` are picks as fractions of the view; measure modes are numbered as in `MeasureTool::Mode`.
- Preview handler: `sidestep-previewtest <dll | registered> file.step out.png [seconds] [dragPixels]`. `registered` goes through prevhost.exe.
- Thumbnails: `sidestep-thumb in.step out.png`. Explorer caches thumbnails per file, including failures; copy a file to a new name to force a fresh request.
- Opt-in debug log for the shell DLL: `HKCU\Software\sideSTEP` value `DebugLog` = file path.
- Never capture the owner's screen to test.

## Next steps
1. Collect the owner's hands-on feedback, and a friend's first run of the setup program on a PC without the build tools.
2. Load time on big files: cache meshes on disk keyed by file hash (would make previews and repeat opens near-instant); skip shape healing.
3. Viewer: drag the section plane and box faces in the view; snapping to edge midpoints and circle centres; light/dark following the system theme; IGES; recent files; text in the 3D view.
4. Preview pane: progress while loading; edge lines like the thumbnail.
5. Releases: `v0.2.0` is published as a pre-release with the setup attached (the binaries link OCCT and Qt statically under the LGPL; the README and release notes point to the source and build scripts as the relinking materials). Each new release needs the owner's go-ahead. Still to do: code signing; branded wizard images; detect STEP Explorer Preview in the installer.
6. Property columns in Explorer.

## Machine notes
- sideSTEP is installed per user at `%LOCALAPPDATA%\Programs\sideSTEP` from the setup program. Do not also run `install.ps1` here: it would add a second Installed-apps entry.
- Inno Setup 6.7.3 is installed per user (`%LOCALAPPDATA%\Programs\Inno Setup 6`).
- sideSTEP is the default for .step. Other STEP apps here: FreeCAD 1.1 and F3D.
- This repo commits as "Adrian Samarra" with the samarrastudios GitHub no-reply address and pushes as `samarrastudios` (repo-local git config; the global identity on this machine is a work account and must not be used here).
- If the repo folder is renamed or moved, delete `build/` and re-run `scripts\build.ps1`: CMake caches absolute paths. OCCT and Qt come back from the vcpkg binary cache in a couple of minutes.
- The `gh` tool's active account on this machine is the work account. For this repo run it with the personal account's token for that command only (`GH_TOKEN=$(gh auth token --user samarrastudios)`); do not `gh auth switch`.
- Claude Code's safety check sometimes refuses to run `scripts\install.ps1` or the setup program because they start the autostart helper; when that happens, the owner runs it.

## Conventions
- A failed thumbnail or preview must fail quietly; never show UI or crash the host.
- `build.ps1` kills any dllhost or prevhost holding the DLL before linking.
- Registry paths in C++ are raw string literals (`LR"(...)"`). An unknown escape (MSVC C4129) is a compile error in the viewer and shell DLL: a dropped backslash once made `--register` write junk keys at the root of HKCU while reporting success.
- When generating source from a shell heredoc or a script, check that backslashes survived; prefer writing the file directly.
- In PowerShell never `Start-Process -Wait` on `--helper-on`: it also waits for the helper, which never exits.
- User-facing text: plain words, no first person, and every message says what to do next.
