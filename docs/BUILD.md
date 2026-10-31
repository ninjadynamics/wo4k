# Building and release assembly

The main instructions are [HOW-TO-COMPILE.md](../HOW-TO-COMPILE.md).
Use `make` with your existing compiler/Make installation, or optional
`compile.ps1` to obtain a local portable toolchain through WinGet.

- `make all`: fetch/check dependencies and build mod outputs.
- `make test`: run offline source/GUI/installer/updater checks.
- `make release`: build, test, package under `dist/`.
- `make package`: package already-built outputs.
- `make clean`: delete only named generated mod files.

The source repository tracks C, PowerShell, Make, configuration, licenses,
artwork and JSON patch recipes. EXEs, DLLs, ICOs, `.wodiff` files, compiler
outputs, dependency caches and release inventories are generated and ignored.
The distributable ZIP intentionally includes the binaries required to play.
Original game executables, archives, CD audio and sound effects are never
release inputs.

Setup runs offline on Windows 10/11 x64 with built-in Windows PowerShell 5.1,
.NET Framework, WPF and System.Drawing. The in-process mod runtime is 32-bit;
the controller host, audio processor and launchers are 64-bit.

`make deps` uses `manifests/dependencies.json` to fetch/check upstream SDL3,
libiconv and DDrawCompat packages, plus a portable zstd unpacker. Downloads
stay under `.work/deps/`. Their exact archive and extracted-file hashes are
verified. No dependency is installed globally. Runtime licenses are retained
under `licenses/`; see THIRD-PARTY.md there.

Build-Launchers generates seven-size ICO files from assets/icons PNGs, then
uses windres to embed cyan for Setup and red for Play. The Ninja Dynamics UI
logo remains the supplied 256x128 PNG verbatim. GitHub was resized with:

```powershell
ffmpeg -i github-original.png -vf scale=105:-1:flags=lanczos -frames:v 1 assets/ui/github.png
```

That image is checked in; FFmpeg is not a build or installation dependency.
The UI compensates for Windows DPI to display branding at native pixel size.

Text recipes in `src/patches/` generate checksum-guarded XOR binary differences.
`Prepare-Release.ps1` is a separate maintainer tool for authoring new recipes
from locally owned supported game files; it is not required by normal builds.
The checked-in recipes suffice to build the existing patches without a game.

`VERSION` labels releases. Push a matching `v<VERSION>` tag after the source
commit to trigger `.github/workflows/release.yml`. It builds a fresh checkout,
runs checks, uploads build artifacts and attaches the ZIP/checksum to a GitHub
Release. A branch push alone never publishes binaries. No history rewrite or
force-push is required to remove generated files from the current source tree.

`source.url` and `release.url` point to this WO4K repository and its latest
release. See [UPDATES.md](UPDATES.md) for accepted formats and preservation rules.