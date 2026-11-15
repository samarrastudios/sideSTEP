<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/brand/github/readme-header-dark.svg">
  <img src="assets/brand/github/readme-header-light.svg" alt="sideSTEP: STEP files, seen without the CAD package." width="420">
</picture>

# sideSTEP

See STEP files without opening a CAD package: thumbnails in File Explorer now, a lightweight 3D viewer next.

Open source, for Windows. Created by Adrian Samarra.

## Status

| Piece | State |
|---|---|
| STEP loading, assembly colors, tessellation (`core/`) | Working on Windows (MSVC 2022, OpenCASCADE 8.0.1) and on Linux (OpenCASCADE 7.6) |
| Software thumbnail renderer (`core/renderer.cpp`) | Working |
| Command-line tester (`cli/`) | Working |
| Explorer thumbnail DLL (`shellext/`) | Working: thumbnails confirmed in File Explorer on real assemblies. **Loses to "STEP Explorer Preview" if that Store app is installed** (see Known limits) |
| Qt viewer (model tree, orbit, measure, explode) | Not started |
| Installer / signing | Not started |

## Build (Windows)

Needs Visual Studio 2022 or newer (or Build Tools) with **Desktop development with C++**, and Git.

```powershell
scripts\build.ps1      # first run builds OpenCASCADE: 20-60 minutes, once
scripts\test.ps1       # renders samples\*.step to PNGs and opens the folder
scripts\register.ps1   # turns on thumbnails in Explorer (per-user, no admin)
scripts\unregister.ps1 # turns them off again
scripts\install.ps1    # installs for this user under %LOCALAPPDATA%\Programs\sideSTEP, listed in Settings > Apps
scripts\uninstall.ps1  # removes that install
```

Thumbnails not appearing? Set the string value `DebugLog` under `HKCU\Software\sideSTEP` to a file path; the DLL then appends one line per step (load, initialize, result code) to that file. If the file stays empty, Windows is not calling sideSTEP at all, which usually means another handler has priority.

If PowerShell refuses to run scripts: `powershell -ExecutionPolicy Bypass -File scripts\build.ps1`.

## Layout

- `core/` - portable C++: STEP bytes in, pixels out. No Windows code. Shared by everything.
- `shellext/` - the COM DLL Explorer loads (`IThumbnailProvider` + `IInitializeWithStream`).
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
- OpenCASCADE is LGPL 2.1 (with an exception). This build links it statically for a single self-contained DLL, which is fine for personal use; before distributing, either switch to the dynamic triplet (`x64-windows`) or make the object files available, per the LGPL.

## License

sideSTEP's own code is released under the [MIT License](LICENSE). Copyright (c) 2026 Adrian Samarra.

It builds on [OpenCASCADE Technology](https://dev.opencascade.org/), which is licensed separately under the LGPL 2.1 with an exception. The wordmark uses JetBrains Mono (SIL Open Font License 1.1), converted to outlines.
