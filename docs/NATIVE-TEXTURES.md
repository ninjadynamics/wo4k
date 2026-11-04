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

For an existing installation, without reinstalling sound/music:

```powershell
./scripts/Install-NativeTextures.ps1 -PsxSource 'D:/WipEout 2097.cue'
```

For maintainers, the offline audit is reproducible from extracted originals:

```powershell
python tools/Build-TextureSelection.py --pc-root wobble --ps1-root .work/psx-sprite-source --output manifests/texture-selection.json --audit docs/texture-selection-audit.json
```

The Python/Pillow/numpy audit tools are maintainer tools and are not required
by Setup or the game. The checked-in audit contains hashes/metadata only;
original game assets remain local and are not packaged with the mod.

The native `raceTracks[]` order is **01, 08, 13, 20, 02, 17, 06, 07**, matching
`NRACE01` through `NRACE08`. Sorted track directory numbers are not sheet
numbers. This order was verified in the supported executable and against
native texture placements.
