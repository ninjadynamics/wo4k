# Deband - fast C batch debanding and dithering

A C99 command-line utility that accepts one image or recursively processes a directory.

- FFmpeg `deband` (16-bit processing) is the default when FFmpeg is installed.
- Without FFmpeg, or with `-s`, an all-native C four-sample debanding approximation is used.
- Both modes apply high-pass, blue-noise-like dithering before 8-bit quantization.
- **Band ramps** run first in both modes. Artwork reduced to a few colours
  (a 16-colour PlayStation texture) shows a gradient as flat bands whose steps
  are far larger than the threshold, which the filters above leave alone. Each
  band is redrawn as a ramp towards its neighbours: a pixel moves by at most
  half the step, steps larger than `-b` are treated as edges and kept, and a
  wide flat area changes only within `-w` pixels of a neighbouring band. A
  band one pixel wide is already one sample of the gradient and is unchanged.
  Only a step of a staircase counts as a band (a lighter neighbour on one
  side, a darker one on another); an area lighter or darker than everything
  around it is a detail and keeps its colour. `-k` scales that protection:
  1 leaves details alone, 0 ramps them like any band (smoother but softer).
- **Transparency safety:** All alpha values remain unchanged. Pixels with alpha = 0
  retain all four original RGBA bytes, including the hidden RGB channels, and
  are never dithered. Native mode also ignores zero-alpha neighbors when debanding
  visible pixels.
- Multithreaded **across images**; default 4 workers for FFmpeg or up to 8 for native C.
- Built-in image I/O supports PNG, JPEG, BMP and TGA, with no Python dependency.
- No output is silently replaced except with `-x`. Same-directory temp files make
  in-place replacement atomic on typical filesystems.
- Exits nonzero if any image fails.

Running `deband` with no arguments prints the help text and exits successfully.

## Usage

```text
Usage: deband [options] FILE_OR_DIRECTORY

  -o DIR    Output directory (default: .)
  -x        Overwrite inputs in place, atomically (cannot combine with -o)
  -s        Force native C mode (otherwise uses FFmpeg when available)
  -j N      Concurrent image workers (default: 4 FFmpeg, up to 8 native C)
  -t F      Threshold 0.00003..0.5 (default: 0.020)
  -r N      Radius 1..128 pixels (default: 24)
  -d F      Dither intensity 0..4 levels (default: 0.8)
  -b F      Band step limit 0..0.5 (default: 0.160; 0 = no band ramps)
  -w N      Band ramp reach 1..128 pixels (default: 12)
  -k F      Keep details 0..1 (default: 1.00; 0 = ramp light/dark details too)
  -z        Trim only: losslessly recompress PNGs in place (no debanding)
  -h        Show help
```

Examples on Windows:

```bat
deband.exe image.png
deband.exe photos -o output
deband.exe -s -j 12 photos -o output
deband.exe -x image.png
deband.exe -x photos
deband.exe -x -s photos
```

When `-o` is omitted, output goes to the current working directory and images
are named with a `deband_` prefix. Explicit `-o .` behaves identically.
When the output folder is also the source folder, the prefix is used as well.
In directory mode, the source subdirectory structure is preserved.

Examples:

```text
input/level1.png      -> output/level1.png     (-o output)
input/title.jpg       -> output/title.jpg.png  (-o output)
input/sub/art.tga     -> output/sub/art.tga.png
input/level1.png      -> ./deband_level1.png   (implicit -o .)
input/sub/art.tga     -> ./sub/deband_art.tga.png
```

Output is PNG unless `-x` is supplied. Keeping the original extension before
`.png` avoids collisions between `sample.jpg` and `sample.png`.
An existing output file causes an error; the source is untouched.
On a directory scan with a prefixed output, files beginning `deband_` are
skipped to avoid repeatedly processing prior output files.

`-x` keeps the original format, including JPEG, and replaces each source using
an on-disk temporary file and an atomic rename. JPEG is re-encoded at quality
95, so **in-place JPEG processing is lossy**. Keep originals if you may want
another treatment. Metadata such as EXIF/ICC may not survive processing.

## Lossless PNG trimming

PNG output is recompressed losslessly before it is published, and `-z` does
only that, in place, for one PNG or every PNG in a directory:

```bat
deband.exe -z image.png
deband.exe -z assets\hd
```

The image stays exactly as encoded: size, colour type, bit depth, interlacing,
palette, transparency and every sample, including colour hidden under zero
alpha. Only the row filters and the deflate stream are chosen again: each
strategy is ranked at a fast zlib level, and the two best are compressed at
level 9. Colour-space chunks (`cHRM`, `gAMA`, `iCCP`, `sBIT`, `sRGB`, `cICP`,
`mDCv`, `cLLi`) are kept verbatim; editing metadata (text, EXIF, XMP,
Photoshop data, timestamps, resolution) is dropped. The result is decoded and
compared with the original before anything is written, a file is replaced
only when the result is smaller, and never if it changed on disk meanwhile.
Animated PNGs are left unchanged. The code is in `pngtrim.c`.

## Compile on Windows (MSYS2 UCRT64)

In the WO4K repository, `mingw32-make deband` builds it with pinned libraries
and no MSYS2; see [LOCAL.md](LOCAL.md). Elsewhere, use the **MSYS2 UCRT64**
terminal (not the plain MSYS terminal).

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-libjpeg-turbo mingw-w64-ucrt-x86_64-zlib make
```

Compile a statically linked executable:

```bash
gcc -O3 -std=c99 -D_CRT_SECURE_NO_WARNINGS -static -static-libgcc -o deband.exe deband.c pngtrim.c -lpng -ljpeg -lz -lm
```

Or run `make static` from the UCRT64 terminal.

FFmpeg is **not** linked or bundled. If `ffmpeg.exe` is on your PATH, Deband
uses it automatically. Otherwise it processes images using the native C
algorithm with the same standalone executable.

Windows was not available for compiler testing in the build environment; this
Windows command is provided for MSYS2 UCRT64 and may need adjustment if
packages change.

## Compile on Linux

Install development headers for libpng and libjpeg, then:

```bash
gcc -O3 -std=c99 -D_XOPEN_SOURCE=700 -o deband deband.c pngtrim.c -lpng -ljpeg -lz -lm -pthread
```

For a statically linked Linux binary, run `make static` if the static libraries
are installed.

## Supported formats

| Input | Normal output | `-x` output |
|---|---|---|
| PNG (including alpha) | PNG | PNG |
| JPEG | PNG | JPEG quality 95 |
| BMP (uncompressed 24/32-bit) | PNG | BMP 32-bit |
| TGA (24/32-bit, raw or RLE) | PNG | TGA 32-bit |

PNG/TGA transparency is preserved **exactly at the 8-bit RGBA level**.
Fully transparent pixels retain hidden RGB values as well as alpha = 0.
For FFmpeg mode, original alpha/hidden RGB is restored after filtering.
This adds a source decode for alpha-capable formats. BMP transparency is
preserved when BMP explicitly supplies an alpha mask; otherwise BMP is
treated as opaque.
Other input formats are skipped when scanning a directory.

## Performance / implementation

- A work queue distributes individual images to worker threads.
- Each FFmpeg worker launches one FFmpeg process and streams 16-bit RGBA
  pixel data directly through a pipe, without any disk intermediate images.
- The program applies the dithering and performs output encoding itself.
- The native backend avoids FFmpeg entirely and uses a four-point edge-aware
  debanding operation, with a precomputed direction lookup table.
- The 512x512 high-pass noise tile is shared across all worker threads.
- libpng writes each output quickly; pngtrim.c then recompresses it losslessly.

On the development environment, a run of twelve 510x622 PNGs took 1.31 seconds
with FFmpeg (`-j 4`) and 0.28 seconds with native C (`-s -j 8`). These are
illustrative timings, not guaranteed results on your hardware.

**Quality caveats:** Native C is only an approximation of FFmpeg's deband,
not a pixel-identical port. The generated dither is high-frequency noise,
not an optimized mathematical blue-noise pattern. Both modes save at 8-bit
per channel, and the processing does not retain all source color metadata.

## License note

`deband.c` and `pngtrim.c` are supplied as an example implementation. The build requires
libpng, libjpeg and their respective dependencies, each governed by its own
license. FFmpeg, when installed, is executed as a separate program.
