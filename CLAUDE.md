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

## State at handoff
- `core/` + `cli/` built and tested on Linux with OCCT 7.6; `core/step_loader.cpp` syntax-checked against OCCT 8.0.1 headers (what vcpkg currently ships).
- `shellext/thumbnail_provider.cpp` compiles under MinGW-w64 but has never been built with MSVC, linked against OCCT on Windows, or loaded by Explorer. Expect first-build fixes here.
- `scripts/*.ps1` are untested on a real machine.

## Next steps
1. Run `scripts\build.ps1`; fix whatever MSVC / vcpkg / OCCT 8 linking turns up (toolkit names are handled in CMake via `if(TARGET TKDESTEP)`).
2. `scripts\test.ps1`, check the PNGs look right.
3. `scripts\register.ps1`, verify thumbnails in Explorer; debug by attaching to `dllhost.exe` if they don't appear.
4. Performance: OCCT transfer dominates on large files. Ideas: skip shape healing params, cap by file size with a cheaper fallback, cache meshes on disk keyed by file hash.
5. Then start `viewer/` (Qt6 + TKOpenGl + AIS, XCAF tree in a QTreeView).

## Conventions
- A failed thumbnail must fail quietly (return an error HRESULT); never show UI or crash the host.
- Rebuilding while registered: `build.ps1` kills any dllhost holding the DLL.
