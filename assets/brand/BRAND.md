# sideSTEP brand assets

The mark is a hexagonal solid, half drawn as edges and half filled: a STEP file before and after you can see it.

## Files

| File | Use |
|---|---|
| `icon/sidestep.ico` | Windows app icon. Holds 16, 20, 24, 32, 40, 48, 64 and 256 px. |
| `icon/sidestep-icon.svg` | Master icon on its dark tile (32 px and up). |
| `icon/sidestep-icon-small.svg` | Simplified master for 24 px and below: heavier outline, no inner edge. |
| `icon/sidestep-glyph-on-dark.svg`, `-on-light.svg` | The mark without a tile, in color. |
| `icon/sidestep-glyph-black.svg`, `-white.svg` | One-color versions. |
| `icon/png/sidestep-<size>.png` | PNG exports, 16 to 1024 px. |
| `lockup/sidestep-lockup-light.svg` / `.png` | Icon plus wordmark, for light backgrounds. |
| `lockup/sidestep-lockup-dark.svg` / `.png` | Icon plus wordmark, for dark backgrounds. |
| `github/readme-header-light.svg`, `-dark.svg` | README header with tagline. |
| `github/avatar-512.png` | Repo or org avatar; safe in a circular crop. |
| `github/social-preview.png` | 1280 x 640. Upload under repo Settings > Social preview. |

## Colors

| Name | Hex | Role |
|---|---|---|
| Graphite | `#17191E` | Icon tile, dark backgrounds |
| Paper | `#F3F1EA` | Wire half of the mark, text on dark |
| Signal orange | `#FF5B1F` | Solid half of the mark. The only accent. |
| Ink | `#111316` | Text and wire on light backgrounds |

## Rules

- Name: **sideSTEP** in prose and UI. `sidestep` in code, commands and file names.
- Wordmark: JetBrains Mono, "side" in Light and "STEP" in Bold. In these files it is converted to outlines, so no font is needed to display it. JetBrains Mono is licensed under the SIL Open Font License 1.1.
- Clear space: at least a quarter of the icon's width on every side.
- Minimum size: 16 px, using the small master.
- Don't recolor the halves, add gradients or shadows, rotate the mark, or put text inside the icon.
- On dark backgrounds use the tile-less glyph in lockups; the tiled icon is for app icons.

## Wiring it into the project

README header that follows GitHub's light and dark modes:

```html
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/brand/github/readme-header-dark.svg">
  <img src="assets/brand/github/readme-header-light.svg" alt="sideSTEP: STEP files, seen without the CAD package." width="420">
</picture>
```

Windows icon for an executable or DLL: add a resource file (for example `cli/app.rc`) containing

```rc
IDI_APP ICON "../assets/brand/icon/sidestep.ico"
```

and list it in that target's sources in `CMakeLists.txt`. The future Qt viewer exe should use the same file, and should also call `setWindowIcon`.

## Status

First release of the mark. The 16 to 24 px icons use a simplified drawing but have not been pixel-tuned by hand. Name and mark have not been checked against existing trademarks.
