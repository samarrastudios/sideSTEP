# sideSTEP - project notes for Claude Code

Windows utility: STEP/STP files get real thumbnails in File Explorer, and (next) open in a lightweight native 3D viewer with model tree, pan/zoom/rotate, measure and explode. Owner: Adrian. Built ground-up; comparable product is "STEP Explorer Preview" on the Microsoft Store.

## Decisions already made
- All native C++17. Geometry kernel: OpenCASCADE via vcpkg manifest (`vcpkg.json`), triplet `x64-windows-static-md`.
- Viewer will be Qt + OCCT's AIS/V3d viewer (not web tech).
- Milestone order: (1) Explorer thumbnails, (2) viewer, (3) preview pane + property columns, (4) installer.
- GitHub repo: https://github.com/samarrastudios/sideSTEP (remote `origin`, branch `main`).
- Distribution: source on GitHub, with a downloadable setup `.exe` attached to GitHub Releases. No Microsoft Store, no MSIX. Per-user install (no admin) preferred. Signing is optional and undecided; unsigned builds trigger a SmartScreen warning.
- `core/` must stay free of Windows and Qt headers; `core/thumb_core.h` must stay free of OCCT types.
- Per-user (HKCU) COM registration for development; no admin.
- Once the viewer is installed, sideSTEP should be the default app for opening STEP files (.step/.stp/.p21). The installer registers a ProgID + file type association in the registry; Windows does not let an app take the default silently (UserChoice is hash-protected), so the user confirms once via "Open with > Always" or Settings > Default apps. Belongs to milestones 2 and 4, not the thumbnail work.
- Branding: `assets/brand/` is the full brand packet; read `assets/brand/BRAND.md` before making anything user-facing. Use it everywhere it applies: installer dialogs (icon, lockup, Graphite/Paper/Signal orange palette), the exe/DLL/viewer icons (`icon/sidestep.ico` via an `.rc`), the README header, and the GitHub avatar and social preview. Follow its rules (no recoloring, gradients or shadows; "sideSTEP" in prose, `sidestep` in code).
- Credit line for the installer, About box and GitHub: "Created by Adrian Samarra. Open source, for Windows, released on GitHub."
- The project name lives in `CMakeLists.txt` (`APP_NAME`, `APP_DISPLAY_NAME`).

## State (2026-11-15)
- Builds clean on Windows: Build Tools 2022 (MSVC 19.44), vcpkg baseline pinned in `vcpkg.json`, OpenCASCADE 8.0.1, triplet `x64-windows-static-md`. No source changes were needed for MSVC or OCCT 8. First build ~55 min (OCCT), later builds seconds.
- `scripts\build.ps1`, `test.ps1`, `register.ps1` all run. Sample PNGs are correct. Thumbnail time: ~0.1 s per sample in-process, ~0.2 s through the shell.
- The DLL is verified through the real shell pipeline (IShellItemImageFactory -> thumbnail dllhost) using `.stpnc` copies of the samples.
- **Open problem on this machine:** the Store app "STEP Explorer Preview" (AelyoSoftworks.STEPExplorerPreview) is installed and its packaged thumbnail handler wins for `.step`/`.stp`/`.p21` (and it currently fails, so those files show no thumbnail). Our registry entries are correct and `AssocQueryString` resolves to our CLSID, but the shell still binds the packaged handler. Fix is to uninstall that app; not yet done (owner's call). Longer term, only a packaged handler of our own could compete on equal terms.
- Fixed: host hung at process exit after a thumbnail (OCCT `OSD_ThreadPool` static destructor at DLL detach). `RenderStepThumbnail` now meshes single-threaded. The CLI still meshes in parallel via `LoadStepFile`.
- Added: opt-in debug log (`HKCU\Software\sideSTEP` value `DebugLog` = file path), icon + version resource (`res/app.rc.in`).
- Testing tip: Explorer caches thumbnails per file, including failures; copy the file to a new name to force a fresh request.

## Next steps
1. Resolve the STEP Explorer Preview conflict, then confirm thumbnails for `samples\*.step` / `*.stp` in Explorer at Large icons.
2. Performance: OCCT transfer dominates on large files. Ideas: skip shape healing params, cap by file size with a cheaper fallback, cache meshes on disk keyed by file hash. Not yet measured on Windows with a large assembly.
3. Catch structured exceptions (access violations inside OCCT) so a bad file cannot take down the thumbnail host; today only C++ exceptions are caught.
4. Start `viewer/` (Qt6 + TKOpenGl + AIS, XCAF tree in a QTreeView).
5. Installer (owner wants this next; go-ahead to install Inno Setup and to publish was still pending on 2026-11-15): (a) switch the triplet to `x64-windows-static` so the DLL does not need the VC++ runtime on other PCs (rebuilds OCCT, ~50 min; set the MSVC runtime to static in CMake to match); (b) Inno Setup script doing what `scripts/install.ps1` does, per-user, branded wizard pages, detects STEP Explorer Preview and tells the user; (c) attach `sideSTEP-Setup-<version>.exe` to a GitHub Release, labelled preview. Unsigned for now.

## Machine notes
- sideSTEP 0.1.0 is installed per-user at `%LOCALAPPDATA%\Programs\sideSTEP` via `scripts\install.ps1` (listed in Installed apps).
- This repo commits as "Adrian Samarra" with the samarrastudios GitHub no-reply address and pushes as `samarrastudios` (repo-local git config; the global identity on this machine is a work account and must not be used here).
- If the repo folder is renamed or moved, delete `build/` and re-run `scriptsuild.ps1`: CMake caches absolute paths. OpenCASCADE is restored from the vcpkg binary cache in about a minute, not recompiled.

## Conventions
- A failed thumbnail must fail quietly (return an error HRESULT); never show UI or crash the host.
- Rebuilding while registered: `build.ps1` kills any dllhost holding the DLL.
