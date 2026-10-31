# How to compile WO4K

Build on Windows 10/11 x64. The repository contains C/PowerShell source,
artwork, configuration, licenses and text patch recipes. Compiled EXEs/DLLs,
generated ICOs, binary differences and release inventories are ignored by Git.
Ready-to-use binaries are distributed through GitHub Releases.

## Optional local bootstrap

From Windows PowerShell in the repository root:

```powershell
.\compile.ps1
# Build, run offline checks, and create a distributable:
.\compile.ps1 -Target release
```

`compile.ps1` checks **only `.tools/`** for a complete compiler/Make bundle.
If missing, it uses WinGet to download the portable
`BrechtSanders.WinLibs.POSIX.MSVCRT.LLVM` ZIP and unpacks it under
`.tools/toolchain/`. This bundle includes GCC, Clang/LLVM and GNU Make.
WinGet verifies the download against its package manifest. The script then
runs that local `mingw32-make.exe`. It reuses the local tools on later runs.

It does not install a compiler globally, edit persistent PATH, or require an
administrator terminal. PATH changes last only for that script. WinGet itself
must already be available through Windows App Installer. Downloads need network
access and disk space for the portable compiler bundle. A failed download/build
stops the script; it never falls back to an unrelated global installer.

You can change the local tools directory with `-ToolsDirectory`, or select
`-Target all`, `test`, `package`, `release`, `clean` or `help`.

## Existing toolchain: Make directly

Experienced users do not need `compile.ps1` or `.tools/`. Use your own
MinGW-w64 GCC, Clang, llvm-dlltool, lld-link, windres and native GNU Make.
The compiler directory must include MinGW Windows headers in its sibling
`include/` or `x86_64-w64-mingw32/include/` tree. Windows PowerShell 5.1,
Windows curl.exe and Windows tar.exe are used by the build scripts.

```powershell
# With gcc.exe on PATH (make may be called mingw32-make.exe):
make
make test
make release

# Or select a compiler directory explicitly:
C:\mingw64\bin\mingw32-make.exe release TOOLCHAIN=C:/mingw64/bin
```

The Makefile is the build entry point; its implementation scripts are `.ps1`.
It uses native Windows cmd/PowerShell, so run it from PowerShell or Command
Prompt. `make all` downloads missing dependencies and builds the mod without
packaging it. `make release` builds, tests, and packages. Individual targets:

| Target | Result |
| --- | --- |
| `deps` | Download/check cached third-party dependencies |
| `runtime` | Build the 32-bit in-process mod DLL |
| `controller` | Build the 64-bit SDL3 controller host |
| `audio` | Build the local C audio processor |
| `launchers` | Generate multi-size icons and embed them in Setup/Play |
| `patches` | Compile text XOR recipes into checksum-guarded `.wodiff` files |
| `test` | Script/GUI, installer primitives and offline updater checks |
| `package` | Package existing outputs without rebuilding |
| `release` | Build, test and package |
| `clean` | Remove named generated mod outputs; retain game data/tools/cache |

## Dependencies and outputs

`make deps` fetches pinned, SHA-256-verified archives into `.work/deps/`:
SDL3 3.4.16, libiconv 1.18, DDrawCompat 0.7.2 and a portable zstd unpacking
helper. Archive URLs and both archive/extracted-file hashes are recorded in
`manifests/dependencies.json`. Third-party build/runtime downloads stay out of
Git. The SDL host links static SDL3 and uses the replaceable libiconv DLL;
DDrawCompat is an unmodified upstream runtime dependency.

The runtime, audio processor and launchers are built from this repository's
source. No original game files are needed to compile or run the default offline
tests. Text patch recipes in `src/patches/` encode only binary differences and
original/output checksums. The installer applies them to the user's own game.
Maintainers authoring new game patches can run Prepare-Release.ps1; it also
exports their reviewable JSON recipes with Export-PatchRecipes.ps1.

Build outputs remain in their runtime/launcher locations for local testing and
release assembly. Packaging creates:

```text
dist/wo4k-<VERSION>.zip
dist/wo4k-<VERSION>.zip.sha256
dist/release-files.json
```

`VERSION` is the version source of truth. An explicit `VERSION=...` Make override must match that file, so installer receipts and package labels agree. Use a matching version tag for publication. The package includes source, runtime binaries, artwork,
licenses and setup scripts, but no original game executable/data/audio.

Tests that inspect the actual game or launch it require your local installation
and are separate from `make test`; see the scripts under `tests/`. Rebuilding a
mod does not establish that gameplay was tested on a real desktop.

## Publish a GitHub Release

A normal `git push` updates source only; it does not create a release.
After committing the intended sources and setting `VERSION`:

```powershell
git push origin master
git tag v0.1.0-beta.13
git push origin v0.1.0-beta.13
```

The tag workflow in `.github/workflows/release.yml` builds from a fresh source
checkout on Windows, runs offline checks, and creates a full GitHub Release
with the ZIP and checksum. A full release with a beta version name is used so
Setup's `/releases/latest` updater can discover it. The workflow needs Actions
enabled and `contents: write`; it uses GitHub's job token, not a stored secret.
A manual workflow run keeps build artifacts without publishing a release.

Alternatively, upload a locally verified ZIP and its `.zip.sha256` sidecar to
a release through GitHub's UI or `gh release create`. GitHub's automatic source
archives are not install/update packages. See [docs/UPDATES.md](docs/UPDATES.md).
