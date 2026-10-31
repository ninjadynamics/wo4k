# Building and release assembly

Setup runs offline on Windows10/11 x64 with built-in Windows PowerShell5.1,
.NET Framework, WPF and System.Drawing. Native game rendering is32-bit; the
SDL3 controller host, audio processor and launchers are64-bit.

Build dependencies for maintainers: MinGW-w64 GCC/Clang, llvm-dlltool/lld-link,
and the static SDL3 SDK used by the controller host. No end user needs them.

```powershell
.\scripts\Build-Runtime.ps1 -CompilerDirectory 'C:\mingw64\bin'
.\scripts\Build-Audio.ps1
.\scripts\Build-Controller.ps1 -SdlDirectory 'D:\SDK\SDL3'
.\scripts\Build-Launchers.ps1
```

Build-Launchers uses GCC and windres from the same compiler directory. It
creates seven-size ICO files from the supplied assets/icons PNGs and embeds
them as native icon resources: cyan for Setup, red for Play.

The Ninja Dynamics UI logo remains the supplied 256x128 PNG verbatim.
The GitHub UI logo was resized with ffmpeg's Lanczos filter to 105x24:

```powershell
ffmpeg -i github-original.png -vf scale=105:-1:flags=lanczos -frames:v 1 assets/ui/github.png
``` 

The UI compensates for Windows DPI so each branding raster displays at its
actual pixel dimensions, preserving the logo aspect ratio.

The SDL SDK layout is include/SDL3/, lib/libSDL3.a and libs.txt (one line of
MinGW link flags). The host statically links SDL3 and dynamically links GNU
libiconv, allowing replacement of that LGPL library without rebuilding the
host. Build-Controller copies libiconv-2.dll from the toolchain's bin/ folder.
Sources: https://github.com/libsdl-org/SDL and https://www.gnu.org/software/libiconv/.
GNU libiconv's library license and original README are supplied in licenses/.
The unmodified library can be rebuilt/replaced independently; it is not part
of the MIT license. SDL3's own license is also retained.

The original game's verified entry bytes/reference tables are checked in as
small hook descriptors, not an original executable. Runtime hooks validate
their expected instructions before altering memory. Setup checks full-file
input and output hashes before installing its sparse XOR binary differences.

Maintainer fixture generation uses locally owned game files:

```powershell
.\scripts\Prepare-Release.ps1 -SourceGameRoot 'D:\Dev\Wobble' -DiscGameRoot 'D:\ExtractedPcDisc'
.\scripts\Build-Release.ps1
```

Prepare-Release reads supported executables and sound hashes locally. No game
source file is copied into the distributable. Build-Release uses an explicit
allowlist, rejects original game/audio payload extensions and validates every
manifest checksum. Private wobble/, .work/, backups/, exports/, dist/ and logs/
are excluded. Distribution ZIP and checksum are written under dist/.

No GitHub upload is performed by these scripts. Tag a reviewed commit and attach
the verified ZIP/checksum to its GitHub release after the desktop play test.

The supplied `release.url` and `source.url` point to the WO4K releases and
source repository. Change them before packing a fork; leave either empty to disable its
control. The updater uses the Windows curl.exe already on the user's system.
See UPDATES.md for accepted URL formats and package requirements. Run
`tests/Test-Updates.ps1` to check URL states, package verification, backups,
rollback and preservation of installed game data using offline fixtures.
