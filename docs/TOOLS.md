# Advanced use and maintainer tools

Players need only Setup and Play. Everything the old standalone scripts did
(extracting music, effects and textures, applying the executable patch,
refreshing assets) is built into them.

## Play options

```bat
Play.exe --refresh-only       :: check and repair the installation, then exit
Play.exe --no-error-dialog    :: report failures only in logs\
```

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

In a source checkout, double-click `assets/hd/trim.cmd` with FFmpeg on PATH.
It processes PNGs beside the script, verifies decoded pixels/alpha and bit
depth, preserves PNG colour-profile chunks, and replaces only smaller outputs.
Editing metadata is removed. Animated PNGs are skipped. Use `--no-pause` from a
terminal to suppress the final keypress prompt. This maintainer script runs
through Windows PowerShell; it is kept in Git and excluded from game release
ZIPs.

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

## Maintainer texture debander

The user-supplied C debanding utility is in `tools/deband/`. Build it with its
own Makefile against static libpng, libjpeg and zlib, for example from the
MSYS2 MINGW64 shell:

```sh
pacman -S mingw-w64-x86_64-libpng mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-zlib
mingw32-make -C tools/deband static
```

`-h` shows the available options. Use `-o` with a separate output directory to
retain the source images. FFmpeg is preferred when installed, with a native C
fallback. See [the local tool notes](../tools/deband/LOCAL.md). The executable
and processed artwork remain ignored, and this maintainer tool is excluded from
game releases.
