# Third-party components

DDrawCompat is redistributed unmodified under0BSD. Its original license is
included. See manifests/third-party.json for its exact DLL version/checksum.

SDL3 is statically linked into our controller host under the zlib license,
included as SDL3.txt. Its bundled HIDAPI notice is retained in HIDAPI.txt under
HIDAPI's original permissive license. The host dynamically links GNU libiconv 1.18; its unmodified
DLL, LGPL library license and original README are supplied. You may replace
libiconv-2.dll independently with a compatible build. Corresponding library
source: https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz .
The MSYS2 package recipe is available in https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libiconv .

Sources and build instructions for the host, audio processor, runtime and
launchers are included. GCC/MinGW runtime linking is covered by the GCC Runtime
Library Exception and MinGW-w64 runtime terms; these do not change the mod's
MIT license. Windows system DLLs are supplied by Windows, not by this package.

The mod's MIT license does not relicense these dependencies or original game
artwork. No original game executable, data archive, CD audio or sound-effect
file is included in the download.
