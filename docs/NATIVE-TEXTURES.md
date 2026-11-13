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

With a PlayStation disc imported, the plan's `scenery` tracks take the
disc's `TRACK.WAD` in place of the PC one (kept under
`textures/native-source/`): the format is the same, the PlayStation scenery
models are the later build, and the PC game places scenery textures where the
archive's own TIM headers say. Their sheets are then laid out as PlayStation
video memory (sheet page 0 is VRAM x 320), and HD region placements on those
sheets are found again by picture rather than by PC coordinates. Talon's
Reach, whose model is the same on both, takes the disc's archive with three
of its textures in other places (the plan's `moves`, a texture's number and
its place in video memory), because the PC game's own code for that track
expects three things of the sheet:

- Its smoke routine takes the smoke models' texture to be on the sheet's
  ninth page, where the PC archive has it, and readies the two pages the
  smoke is drawn from only when it finds it there. The PlayStation keeps
  that texture on the sixth page; the game then draws from pages it never
  readied, and stops. The texture goes back to the PC's place.
- Its board routine shows whatever is at the place of `TH.TIM`, which on
  the PC's sheet is the lucky cat. The PlayStation has a scenery texture
  there; it moves to free room on the ninth page, and the cat stays.
- That routine changes only where on a page the boards' panels look. The
  page stays the one their tiny placeholder texture is on: the cat's page
  in the PC archive, where the placeholder lies inside the cat's picture,
  and another in the PlayStation's, where the boards showed pipes. The
  placeholder goes to the PC's place and is not pasted into the sheet
  (`"paste": false`).

The runtime enlarges the game's 1.8 MB race arena, which Gare d'Europa's
PlayStation model overflows.

The archive put in place is not always the disc's byte for byte. The
PlayStation scenery has large trackside boards (objects named `redb...`)
that its game covers with Red Bull advertising while it runs; in the
archive each is four panels on a tiny placeholder texture, and the PC game
has no code for them outside Talon's Reach. Left alone they stand as dark
panels. `wo_boards.c` fills them in:

- Where the PC archive has a board at the same place (it replaced some
  with a single panel showing a picture of its own: Sagarmatha's four
  "FEED ME" boards, Vostok Island's cat, two on Spilskinanke, one on Odessa
  Keys), the installed archive takes the PC's board: its model record,
  renumbered for the PlayStation textures.
- The other thirteen (one on Gare d'Europa and Odessa Keys, five on
  Valparaiso, six on Phenitia Park) take, in turn, the pictures the track's
  `boards.pictures` in the map lists: `scene:<n>`, one of the track's own
  scenery textures (its sponsors' and shop signs), or `advert`, the first
  picture of the track's `boards.adverts` file (`TEXTURES/...RED.CMP`, the
  Red Bull logo; the disc names these files after Greek letters whose
  place in the alphabet is the track's number). A quarter of the picture
  goes on each panel, as the game's own board routine lays one out.

The PlayStation's routine shows the advert file's two pictures by turns,
the logo and a slogan, changing every twentieth frame; the PC's shows one
and never changes. The runtime does it on the texture page
(`src/runtime/board_adverts.c`): where the track's archive holds the logo,
its place on the page is painted with the slogan, stretched to that place
as the panels stretch it over the board, and with the logo again, twenty
simulation steps each. The logo is whatever the composed page shows. The
slogan is `textures-<letter>red-cmp-1.png` from `assets/auth/` or
`assets/hd/` if there is one, else the disc's picture with its debanded
colours (the installer debands it with the rest, though it is on no sheet).
Talon's Reach has no board with the logo: there the PC game's own board
routine shows the lucky cat, still.

A picture the PlayStation scenery textures do not include is added after
them: the advert where its own header places it, which the PlayStation
layout keeps clear, and a PC picture where the PC has it if that is clear,
else in the advert's place on a track that does not show the advert, else
where a PC scenery texture large enough used to be. A changed texture file
is stored uncompressed, which the game reads as well.

Otherwise packed TGA sheets are rebuilt without changing PC model references, UVs,
track geometry or texture indices. One exception corrects the PC release's
data: the map's `patches` change a single byte of `TRACK17/TRACK.WAD`
(verified by the file's SHA-256 before and after), where `scene.cmp` places
the grass tile four texels over the wall tile beside it. The PlayStation
release has them side by side, and placements marked with that patch lay
the sheet out the same way. They are skipped where the file is not the
retail one. Original sheets are retained under
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
canvases and orientation. See-through texels (exact native black, which the game
keys on every sheet) are exported transparent; the PlayStation's opaque black
stays opaque black. Keep the alpha channel through upscaling: a replacement with
transparency is drawn by its own alpha, one without by the native cut-out.
The exporter verifies the installed receipt and exact pixel reconstruction, never
rewrites game/HD files, and refuses to overwrite an existing workspace. Exported
individual images need packing into HD pages after visual approval; they are not
direct `assets/auth` replacements. All exported original game artwork stays ignored
by Git and is excluded from releases.

`manifests/hd-textures.json` registers all 576 usable exported texture filenames
as `kind: "region"` entries, alongside the existing UI aliases. Each entry gives
its original dimensions and every native sheet/page rectangle. The three legacy
`screen0` screenshot sections are marked as reference images, not installable
texture slots. No original game image pixels are included in the manifest.
The 27 edited signs, logos, lucky cats and scene textures currently in
`assets/auth/` are mostly at half their edited export dimensions (2x native).
The two cat billboards use alpha cutouts from their edge-connected near-black
matte; faint background grain is removed while enclosed dark details stay opaque.
The original full-size edited PNGs stay in the local export workspace.

Artwork is looked for in four places, best first: `assets/auth/` (authored,
shipped with the mod), `assets/hd/` (upscaled sets, made with `tools\upscale.cmd`;
not shipped), `assets/sd/` (the installer's debanded pictures, below) and the
sheet itself. A PNG in `assets/hd/` is used where `assets/auth/` has none of
that name. The catalogue also lists the 75 frames that only the PlayStation's
scenery has, with no place in the PC layout (`"regions": []`): their places
are found by their pictures, so every name in `assets/sd/` is a name the two
folders above accept.

Drop a matching exported PNG directly into `assets/auth/` (or `assets/hd/`)
and launch Play. The
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

One scenery picture is not a still: Valparaiso's waterfall
(`track13-scene-cmp-16.png` among the exports). The game moves twelve of its
sixteen colours along at every simulation step, and after each turn the
runtime repaints the picture's place on its page from the track archive's own
texels and the game's palette (`src/runtime/palette_cycle.c`). It finds the
picture as the one the model named `wfall` is drawn with, which holds for the
PlayStation's archive and the PC's. The page keeps whatever size its other
replacements give it, with each texel of the waterfall enlarged to match. A
replacement PNG for the waterfall is painted over, so do not author one, and
its debanded picture (below) is painted over likewise.

## Debanded textures

The PlayStation's textures have 16 or 256 colours, and a gradient shows as
bands. `manifests/deband-selection.json` records, for every texture frame of
the disc, whether to keep it as it is (`original`) or deband it at one of two
strengths (`v1` smoother, `v2` with light and dark details kept). It is the
file the comparison page of `tools/Deband-Ps1Textures.py` exports, unchanged:
to change the choices, export again and replace it.

When the sheets are built (Setup's install, and Play's check when anything
the result depends on has changed), `src/core/wo_deband.c` debands every
chosen picture that the Hi-res sheets show, writes each as a PNG into
`assets/sd/` and lists where each belongs in `wobble/textures/sd-regions.bin`
(the form of `hd-regions.bin`). A sheet holds 15-bit colour, too few levels
for a smooth ramp, so the result cannot go into the sheet: the runtime lays
the PNGs over the sheet's page when it loads, before any HD region. Only
colours are taken, and only where the sheet shows a texel, so cut-outs and
the see-through black of effects stay the sheet's. A page that has a PNG of
its own in `assets/auth/` or `assets/hd/` is left alone. The layer is loaded with the HD
textures and follows their setting.

- What a sheet shows is what its build pasted last: the map's operations,
  then the frames of the PlayStation scenery archive in place. A picture
  partly covered by a later one is not debanded.
- A frame takes its choice by name (`TRACK13/SCENE.CMP#0016`), whether it
  was read from the imported disc or from the PC's byte-identical file. The
  PNG has the name the comparison page gives it (`track13-scene-cmp-16.png`).
- A road is its 32-pixel tiles put together. It is debanded as one picture
  (`track13-road-0-128.png`), each tile at the strength chosen for it, so a
  gradient that crosses tiles gets one ramp.
- Scenery frames whose borders meet in the track's model are debanded with
  a margin of their neighbours, as the comparison page's pictures were.
- Pictures only the PC has (its own boards, the interface) have no choice
  and stay as they are.

The filter is the maintainer tool's own, `tools/deband/filter.c`, built into
Setup and Play. Its four-sample stage takes its samples where FFmpeg's
`deband` does, so without FFmpeg it gives what the comparison page showed:
against the reviewed pictures the installer's differ by 0.9 levels of 255 on
average, the same as a second FFmpeg run, the difference being the dither.
Debanding the 560-odd pictures takes about five seconds. The step is skipped
while the choices, the filter's revision and the sheets are what the layer in
place was made from (`wobble/textures/deband.json`).
