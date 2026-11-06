# Third-party components

**DDrawCompat** (`runtime/ddraw.dll`) is redistributed unmodified under the
0BSD license, included as DDrawCompat.txt. manifests/third-party.json records
its exact version and checksum.

**SDL3** is statically linked into the controller host
(`runtime/wo2097-controller.exe`) under the zlib license, included as
SDL3.txt. Its bundled HIDAPI notice is retained in HIDAPI.txt under HIDAPI's
original permissive license. The host dynamically links **GNU libiconv 1.18**:
its unmodified DLL, LGPL library license and original README are supplied, and
you may replace `libiconv-2.dll` with any compatible build. Corresponding source:
https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz (MSYS2 package recipe:
https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libiconv).

**raylib 5.5** draws the Setup window and is statically linked into
`Setup.exe` under the zlib license, included as raylib.txt. It is built from
its pinned upstream source with only the modules Setup needs. raylib includes
**GLFW 3.4** (zlib license, GLFW.txt) and the public-domain/MIT **stb**
single-file libraries (stb_image for PNG decoding, stb_truetype for fonts),
whose notices are in their source files.

**miniz 3.1.2** (ZIP and deflate) is compiled into Setup, Play and the build
tool under the MIT license, included as miniz.txt.

Sources and build instructions for everything else in this package (the game
runtime, controller host, video player, Setup and Play) are in the project
repository. GCC/MinGW runtime linking is covered by the GCC Runtime Library
Exception and the MinGW-w64 runtime terms; these do not change the mod's MIT
license. Windows system DLLs are supplied by Windows, not by this package.

The mod's MIT license does not relicense these dependencies or the original
game's artwork. No original game executable, data archive, CD audio or
sound-effect file is included in the download.
