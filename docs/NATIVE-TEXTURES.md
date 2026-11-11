# Native PC / PlayStation artwork

Setup restores road materials from the independently paletted 32x32 tiles in
the PC track WADs. These libraries and their layout tables were compared with
the European PS1 disc (SLES-00327) during development and are byte-identical.
The assembled 128x128 materials retain up to 72 colours rather than reducing
each material to the prepared PC image's single 16-colour palette.

The optional **PlayStation game** field accepts that disc's ISO, CUE/BIN or
ZIP. Original CMP/TIM textures and layout companions that are not already
byte-identical PC sources are extracted to `wobble/textures/ps1-source/`.
Music folders still supply music only. Data-only images supply artwork without
enabling the PlayStation soundtrack. Other PS1 regions can supply their CD
audio, but their artwork has not been verified against this fixed map.

`manifests/texture-selection.json` contains the fixed source/placement choices.
PC data is selected only where native TIM/archive bytes were verified equal
to the PS1 source. Other mapped originals use the imported PS1 source when
available, with the existing PC sheet as the fallback before a disc is supplied.
The installer does not compare versions, count colours, calculate quality
scores or choose between assets at runtime. File/format/extent checks still
reject malformed input; these are decoding checks, not version comparisons.

The map names the exact sheet (by SHA-256) each placement was located in: the
retail release's. A game folder can hold an earlier sheet with the same name
and another arrangement of textures, which the game's models do not index. A
sheet is taken as the pristine copy only when it is the named one; otherwise a
retained pristine copy that is stays, and a sheet that is neither is installed
as it is, without placements (the log says `native_sheet_unrecognised`).

Packed TGA sheets are rebuilt without changing PC model references, UVs,
track geometry or texture indices. Original sheets are retained under
`wobble/textures/native-source/`. Refresh rebuilds from these pristine copies
when the map revision changes. Repair retains both the pristine sheets and
imported disc files; the disc is not required for subsequent launches.
Authored HD PNGs keep their normal priority.

The PS1 disc also contains interface layouts, development leftovers and assets
for geometry absent from the PC version. These are retained when imported;
they are not assigned to an unrelated PC texture slot. The audit explicitly
lists frames not located in native PC sheets. A matching source image does not
by itself establish identical PS1 blending, texture filtering or lighting.
Original TIM files retain PS1 transparency/STP information; the PC's packed
RGB555 sheets cannot encode those per-texel compositing rules on their own.

The runtime restores the shared effect emitters' PS1 blend choice: additive
explosions/flashes and subtractive smoke. Original-size billboards retain
animation interpolation and PS1 neutral128 colour modulation. This is not
full PS1 GPU emulation; the mod retains its modern resolution and filtering.

The [exact PS1 choices](PS1-TEXTURE-CHOICES.md) list every disc-sourced frame.
The local comparison page at `exports/ps1-texture-choices/index.html` includes
all 484 mapped textures, overlay sliders and optional JSON selection export.
Roads appear as PS1 artwork variants even though their original tile data is
reused from PC. The current selection is 262 PS1 artwork variants and 222
byte-identical PC originals. Picker choices do not modify the game until
applied; exporting includes hidden/filtered items as well.

To add PS1 artwork to an existing installation, open Setup, click **Repair**,
select the PlayStation source and click **Install**. Saves and preferences are
kept.

For maintainers, the offline audit is reproducible from extracted originals:

```bat
python tools/Build-TextureSelection.py --pc-root wobble --ps1-root .work/psx-sprite-source --output manifests/texture-selection.json --audit docs/texture-selection-audit.json
```

The Python/Pillow/numpy audit tools are maintainer tools and are not required
by Setup or the game. The checked-in audit contains hashes/metadata only;
original game assets remain local and are not packaged with the mod.

The native `raceTracks[]` order is **01, 08, 13, 20, 02, 17, 06, 07**, matching
`NRACE01` through `NRACE08`. Sorted track directory numbers are not sheet
numbers. This order was verified in the supported executable and against
native texture placements.

For a local batch-upscaling workspace, export the complete installed selection:

```bat
python tools/Export-UpscaleTextures.py --game-root wobble --output exports/upscale-textures
```

This maintainer tool requires Pillow/numpy and a complete installed PS1 selection.
It puts editable, original-resolution PNGs in one flat `textures/` folder, with
`manifest.json`, `README.txt` and an immutable `baseline.zip` beside it. Existing
HD atlas pages are excluded. Mapped textures, separate authored road mip levels
and remaining PC atlas sections retain every uncovered native RGB pixel. Equal
high/low-resolution consumers share an image only after decoded equality checks.
The manifest records every destination rectangle, source choice, mip level,
original dimensions and hashes; the ZIP retains original PNGs and native sheets.

Overwrite only the editable PNGs, preserving filenames, aspect ratios, full
canvases and orientation. Native black pixels remain RGB: transparency and effect
blending depend on the draw, so global black-to-alpha conversion is inappropriate.
The exporter verifies the installed receipt and exact pixel reconstruction, never
rewrites game/HD files, and refuses to overwrite an existing workspace. Exported
individual images need packing into HD pages after visual approval; they are not
direct `assets/hd` replacements. All exported original game artwork stays ignored
by Git and is excluded from releases.

`manifests/hd-textures.json` registers all 576 usable exported texture filenames
as `kind: "region"` entries, alongside the existing UI aliases. Each entry gives
its original dimensions and every native sheet/page rectangle. The three legacy
`screen0` screenshot sections are marked as reference images, not installable
texture slots. No original game image pixels are included in the manifest.
The 27 edited signs, logos, lucky cats and scene textures currently in
`assets/hd/` are mostly at half their edited export dimensions (2x native).
The two cat billboards use alpha cutouts from their edge-connected near-black
matte; faint background grain is removed while enclosed dark details stay opaque.
The original full-size edited PNGs stay in the local export workspace.

Drop a matching exported PNG directly into `assets/hd/` and launch Play. The
filename alone selects the texture. Source resolution, aspect ratio and PNG
hashes are not used to choose a slot. PNGs may have arbitrary dimensions and
are never resized or rewritten on disk. Preserve the original logical layout
if you want the same in-game proportions; the existing UV rectangle is filled.

During normal artwork refresh, Play writes an atomic, incremental active index
at `wobble/textures/hd-regions.bin`, containing only present PNGs and their
manifest placements. New filenames and manifest mappings require no runtime
rebuild. Edited PNGs load afresh; removing one restores its native artwork on
the next launch. Invalid input leaves the previous index intact and is reported
before launch. PNG alpha is detected automatically; fully opaque images retain
native black-key semantics. An optional `alpha_mode` of `png` or `black-key`
overrides the default `auto` for a manifest entry.

At native texture
load time, the runtime composes only registered regions over the installed
PC/PS1 sheet; neighboring native artwork is retained without xBRZ processing.
Individual regions, native pages, strips and UI aliases accept rectangular and
non-power-of-two PNGs. Only internal GPU storage uses square power-of-two pages;
the renderer handles that conversion without imposing it on source files.
Existing UI artwork takes its
usual path. World rendering retains native depth, RGB blending and fades, with
separate keyed/opaque uploads so black cutouts do not make opaque surfaces
transparent. Missing optional region artwork falls back to the native sheet.
