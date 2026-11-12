# WO4K maintainer integration

`deband.c`, `Makefile` and the original `README.md` were imported from Bruno's
`deband.zip`. Archive/file hashes and the local Windows patch are recorded in
`provenance.json`. The Windows FFmpeg launcher now restricts child handle
inheritance to its pipe and NUL handles. This fixes intermittent atomic rename
failures with concurrent PNG workers; filtering and defaults are unchanged.

The filter itself lives in `filter.c` / `filter.h` (band ramps, the
four-sample stage, the dither; no files or threads). `deband.c` uses it, and
so does the mod: it is compiled into the core library, and
`src/core/wo_deband.c` debands the game's textures with it at install time.
A change to the filter therefore changes what players get; bump
`DEBAND_REVISION` in `wo_deband.c` with it so that installations rebuild. The
native four-sample stage places its samples as FFmpeg's `deband` does, which
makes the two modes agree but for the dither.

The lossless PNG trimmer (`pngtrim.c`, previously the FFmpeg-based
`assets/hd/trim.cmd`) is part of the tool: `-z` trims PNGs in place, and
debanded PNG output is trimmed before it is published. `assets/hd/trim.cmd` is
now a double-click launcher for `deband -z` on that folder.

Build from the repository root with the same toolchain as the mod:

```bat
mingw32-make deband        :: tools\deband\deband.exe
mingw32-make deband-test   :: pngtrim tests, every result decoded with libpng
```

The first build fetches the checksum-pinned
[libpng source](https://github.com/pnggroup/libpng),
[libjpeg-turbo](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-libjpeg-turbo)
and [zlib](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-zlib)
listed in `dependencies.json` into `.work/deps`; libpng is compiled with the
mod's compiler, which avoids a newer prebuilt library's CRT imports. The
resulting `tools/deband/deband.exe` links the image libraries statically; only
Windows system DLLs are needed. FFmpeg remains an optional separate executable
on PATH. The standalone `Makefile` remains for builds outside this repository.

Run the end-to-end regression fixtures (Pillow/numpy needed for verification
only):

```bat
python tools/deband/Test.py
```

The fixtures exercise both native and installed FFmpeg backends with concurrent
encodes, nested paths/spaces, PNG/JPEG/BMP/TGA, thin images, exact alpha/hidden
transparent RGB preservation, existing-output protection and invalid inputs.
They use generated images under `.work/`; they do not process game artwork.

To process an export into a separate directory:

```bat
.\tools\deband\deband.exe -j 4 -o .\exports\upscale-textures\debanded .\exports\upscale-textures\textures
```

Default settings are threshold `0.020`, radius `24`, dither `0.8`; FFmpeg is
preferred when installed. `-s` forces native C. The tool refuses existing
output files. Use a fresh output directory for another experiment. The original
README documents the other options, including explicit in-place processing.

This is a maintainer tool. Its source can be committed, but generated binaries,
dependencies, exported artwork and processed PNGs are ignored. `tools/` is not
part of the game release payload. Building the tool does not install
processed images into the game; Setup and Play carry the filter themselves.
