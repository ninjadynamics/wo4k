# Advanced use and maintainer tools

Players need only Setup and Play. Everything the old standalone scripts did
(extracting music, effects and textures, applying the executable patch,
refreshing assets) is built into them.

## Play options

```bat
Play.exe --refresh-only       :: check and repair the installation, then exit
Play.exe --no-error-dialog    :: report failures only in logs\
Play.exe --root <folder>      :: use the installation in another mod folder
Play.exe --purge              :: delete every log, crash logs included, and exit
```

Setting the environment variable `WO2097_VIDEO_PREPARE=0` before starting Play
turns off preparing the intro player during the notice screen; the player is
then started when the intro begins, as in earlier releases.

Setting `WO2097_VIDEO_IN_GAME=1` before starting Play makes the game present
the movies itself: the movie player opens no window (`wo4k-video.exe --frames`)
and hands each decoded frame to the game through shared memory. Exclusive
fullscreen needed this; that mode is currently switched off
(`WO_EXCLUSIVE_FULLSCREEN_OFFERED` in `src/core/wo_settings.h`).

## Installing from the command line

There is no separate command-line installer; use Setup. PC game data alone is
enough for the minimum install, and missing PC music is optional. An optional
PlayStation source can supply music (and artwork) for a data-only PC install;
it never replaces PC game data. Choose **No music** for sound effects only.

Close the game before modifying installed artwork, music or effects. Keep your
original inputs.

## HD artwork

HD PNG sources live only in `assets/hd/`; the runtime decodes them directly.
Play validates them before each launch. Setup's HD/original-audio preferences
are retained during every refresh. Setup keeps the original sound effects in
`wobble/audio/source/`; these are locally supplied game files, never release
payloads. Changes to the audio processor are detected and the effects rebuilt.

In a source checkout, double-click `assets/hd/trim.cmd` to losslessly
recompress the PNGs beside it. It runs `deband -z` (see below; build it once
with `mingw32-make deband`), which keeps every pixel, alpha value, bit depth
and colour-profile chunk, removes editing metadata, verifies the result by
decoding it, and replaces a file only when it is smaller and unchanged on disk
meanwhile. Animated PNGs are skipped. Use `--no-pause` from a terminal to
suppress the final keypress prompt. The launcher is kept in Git and excluded
from game release ZIPs.

## Maintainer commands

`wo4k-build` (built by `make tools` as `build\tools\wo4k-build.exe`) reads game
files you own and never modifies them:

```bat
:: SD texture containers plus PNG exports of every TGA, TIM and CMP frame
build\tools\wo4k-build.exe export-textures "D:\Games\Wobble" exports\sd

:: One soundtrack as trackNN.wav plus tracks.ini (pc or psx)
build\tools\wo4k-build.exe extract-music "D:\Discs\Wipeout.XL.mds" exports\pc pc
```

`export-textures` accepts an original PC game folder or an installed `wobble`
folder. PNGs go to `<output>\exports`; CMP frames are numbered by their original
order under `<name>.cmp.png.parts\`. SD PNG exports are painting references,
with native loader row orientation. Native track WAD archives remain with their
geometry. No automatic texture resynthesis or recompression occurs.

`extract-music` accepts a music folder or an MDS/MDF, CUE/BIN image; unpack a
ZIP first.

## Maintainer texture debander and PNG trimmer

The user-supplied C debanding utility is in `tools/deband/`, together with the
lossless PNG trimmer. Build it from the repository root:

```bat
mingw32-make deband         :: tools\deband\deband.exe (fetches pinned libpng, libjpeg, zlib)
mingw32-make deband-test    :: trimmer tests
tools\deband\deband.exe -z assets\hd
```

`-h` shows the available options. Use `-o` with a separate output directory to
retain the source images. FFmpeg is preferred for debanding when installed,
with a native C fallback; trimming needs no FFmpeg. The filter itself
(`tools/deband/filter.c`) is also built into Setup and Play. Debanded PNG output is
trimmed automatically. See [the tool's README](../tools/deband/README.md) and
[the local notes](../tools/deband/LOCAL.md). The executable and processed
artwork remain ignored, and this maintainer tool is excluded from game
releases.

`tools/Deband-Ps1Textures.py --disc <PlayStation ZIP or .bin>` exports every
texture frame on a PlayStation disc to `assets/sd-original/` (see-through
texels transparent), debands the set into `assets/sd-debanded/` and builds a
left/right comparison page in `exports/ps1-deband-comparison/`. Scenery
frames whose borders meet in the track models are debanded with a margin of
their neighbours, so a gradient crossing two frames gets one ramp. Both
folders are generated and ignored. The page exports the choices as JSON:
saved as `manifests/deband-selection.json`, they are what Setup and Play
deband the game's textures by (see
[the texture notes](NATIVE-TEXTURES.md#debanded-textures)). `assets/sd/` is
that step's output, not this tool's.
