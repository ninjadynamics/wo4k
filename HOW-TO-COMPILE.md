# How to compile WO4K

WO4K is plain C built with GNU Make on Windows 10/11 x64. No PowerShell,
Python or .NET is involved. Ready-to-use packages are published on the
[Releases](https://github.com/ninjadynamics/wo4k/releases) page.

## Toolchain

You need a MinGW-w64 toolchain with GCC, Clang, lld (`lld-link`),
`llvm-dlltool`, `windres` and GNU Make (`mingw32-make`). Either of these works:

- **WinLibs** (portable ZIP, nothing installed system-wide):
  `winget download --id BrechtSanders.WinLibs.POSIX.MSVCRT.LLVM`, unpack it,
  and pass its `bin` folder as `TOOLCHAIN=...` (or put it on PATH).
- **MSYS2**, MINGW64 environment: `pacman -S mingw-w64-x86_64-gcc
  mingw-w64-x86_64-clang mingw-w64-x86_64-llvm mingw-w64-x86_64-lld
  mingw-w64-x86_64-make` (this is what the release workflow uses).

Run Make from Command Prompt or any shell; the recipes use `cmd.exe`.

## Build

```bat
mingw32-make deps          :: fetch the pinned third-party archives (once)
mingw32-make               :: build everything
mingw32-make test          :: build and run the automated tests
mingw32-make release       :: build, test and package dist\
mingw32-make TOOLCHAIN=C:/mingw64/bin release
```

| Target | Result |
| --- | --- |
| `deps` | Download and verify SDL3, libiconv, DDrawCompat, raylib and zstd into `.work/deps` |
| `all` (default) | `Setup.exe`, `Play.exe`, `runtime/wohr.dll`, `runtime/wo2097-controller.exe`, `runtime/wo4k-video.exe` |
| `test` | Unit, golden and integration tests (`tests/core`, `tests/runtime`, `tests/video`) |
| `test-local` | Slow end-to-end install from this machine's configured game sources, compared with its existing installation |
| `fuzz` / `fuzz-smoke` | Build the parser fuzz harnesses / run each over its seeds (full libFuzzer runs happen in CI on Linux) |
| `Setup.exe`, `Play.exe`, `core`, `runtime`, `controller`, `video` | Build one component |
| `tools` | `build/tools/wo4k-build.exe`, the build helper, which also has [maintainer commands](docs/TOOLS.md#maintainer-commands) |
| `deband` / `deband-test` | The optional [maintainer debander and PNG trimmer](docs/TOOLS.md#maintainer-texture-debander-and-png-trimmer) / its trimmer tests |
| `sign-input` | Copy the five WO4K binaries to sign into `build/unsigned` |
| `package` | `dist/wo4k-<VERSION>.zip`, its `.sha256` and `release-files.json`; `SIGNED=<folder>` packages signed binaries from that folder |
| `release` | `test` followed by `package` |
| `release-notes` | `build/release-notes.md` from this version's section of `docs/CHANGELOG.md` |
| `clean` | Remove build outputs (keeps dependencies, game data and your installation) |
| `help` | List targets |

`VERSION` is the single version source. `make deps` checks every archive and
an extracted marker file against the SHA-256 values in
`manifests/dependencies.json`; nothing is installed outside `.work/deps`.

No original game files are needed to build or to run `make test`; tests that
use real game data skip themselves when it is absent.

## Layout

```text
src/core/      shared library: formats, installer, updater, logging, settings
src/setup/     Setup.exe (raylib window)
src/play/      Play.exe
src/runtime/   32-bit game DLL (wohr.dll) and the SDL3 controller host
src/video/     wo4k-video.exe, the native movie player
src/tools/     wo4k-build: icons, deps, packaging and maintainer commands
src/common/    version resource and application manifest
third_party/   vendored miniz source
tests/, fuzz/  automated tests and fuzz harnesses
patches/       executable patch recipes (JSON, applied to your own game)
assets/        supplied HD artwork, icons and Setup artwork
manifests/     dependency pins, file catalogues and artwork provenance
tools/         optional maintainer tools (texture audit, debander)
```

## Publishing a release

A normal push builds and tests only. To publish:

```bat
git tag v0.2.0-beta.1
git push origin v0.2.0-beta.1
```

The tag must match `VERSION`, and `docs/CHANGELOG.md` needs a
`## <VERSION>` section: it becomes the release notes. The release workflow
builds from a clean checkout, runs the tests and fuzzers, packages the
binaries and publishes the GitHub Release with the ZIP and its checksum.
Releases are currently unsigned (see
[verifying downloads](README.md#verifying-downloads)). The workflow keeps an
optional SignPath signing step, which runs only when a `SIGNPATH_API_TOKEN`
secret is configured.
