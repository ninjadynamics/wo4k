# WO4K maintainer integration

`deband.c`, `Makefile` and the original `README.md` were imported from Bruno's
`deband.zip`. Archive/file hashes and the local Windows patch are recorded in
`provenance.json`. The Windows FFmpeg launcher now restricts child handle
inheritance to its pipe and NUL handles. This fixes intermittent atomic rename
failures with concurrent PNG workers; filtering and defaults are unchanged.

The PowerShell build helper was retired with the rest of the PowerShell
tooling. Build with the Makefile against static libpng, libjpeg-turbo and zlib,
for example from the MSYS2 MINGW64 shell:

```sh
pacman -S mingw-w64-x86_64-libpng mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-zlib
mingw32-make -C tools/deband static
```

`dependencies.json` still records the checksum-pinned
[libpng source](https://github.com/pnggroup/libpng),
[libjpeg-turbo](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-libjpeg-turbo)
and [zlib](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-zlib)
versions the previous helper used. The resulting `tools/deband/deband.exe` links
the image libraries statically; only Windows system DLLs are needed. FFmpeg
remains an optional separate executable on PATH.

Run the regression fixtures (Pillow/numpy needed for verification only):

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
part of the game release payload. Adding/building the tool does not install
processed images into the game or change Setup/Play.
