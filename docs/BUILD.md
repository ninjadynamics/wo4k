# Building and release assembly

The main instructions are [HOW-TO-COMPILE.md](../HOW-TO-COMPILE.md).

- `make deps`: fetch and verify the pinned third-party archives.
- `make all`: build every mod output.
- `make test`: run the offline unit, golden and integration tests.
- `make release`: build, test and package under `dist/`.
- `make package`: package already-built outputs.
- `make clean`: delete build outputs only.

The source repository tracks C sources, Make files, configuration, licenses,
artwork and JSON patch recipes. EXEs, DLLs, ICOs, compiler outputs, dependency
caches and release inventories are generated and ignored. The distributable
ZIP intentionally includes the binaries required to play. Original game
executables, archives, CD audio and sound effects are never release inputs.

WO4K runs on Windows 10/11 x64 using only components built into Windows
(WinHTTP, BCrypt, WIC, Media Foundation, DirectShow, OpenGL). The in-process
mod runtime (`wohr.dll`) is 32-bit, built with Clang and lld; Setup, Play, the
controller host and the video player are 64-bit, built with GCC. The 64-bit
programs use stack protection, `_FORTIFY_SOURCE`, high-entropy ASLR and DEP,
and carry a version resource and an application manifest. The runtime DLL has
ASLR, DEP and a version resource. Everything builds with warnings as errors.

`make deps` uses `manifests/dependencies.json` to fetch upstream SDL3,
libiconv, DDrawCompat and raylib source, plus a portable zstd unpacker.
Downloads stay under `.work/deps/`. Their exact archive and extracted-file
hashes are verified. No dependency is installed globally. raylib is compiled
from its pinned source with only the modules Setup uses (no audio, models or
screenshot hotkeys). Runtime licenses are retained under `licenses/`; see
THIRD-PARTY.md there.

`wo4k-build` (built from `src/tools/`) is the build-time helper. It generates
the seven-size ICO files from the `assets/icons` PNGs (cyan for Setup, red for
Play), the HD texture catalogue header, `manifests/artwork.json` and
`manifests/third-party.json`, the signing input folder, and the release ZIP.
The ZIP uses fixed timestamps (`SOURCE_DATE_EPOCH` when set), so the same
inputs produce the same package. The Ninja Dynamics UI logo remains the
supplied 256x128 PNG verbatim; the GitHub logo was resized once and is checked
in.

JSON recipes in `patches/` describe checksum-guarded executable patches. Setup
applies them to your own supported game executable; the checked-in recipes
need no game to build or test.

`VERSION` labels releases. Push a matching `v<VERSION>` tag after the source
commit to trigger `.github/workflows/release.yml`. It builds a fresh checkout,
runs the tests and fuzzers, has SignPath sign the five WO4K binaries (after a
maintainer approves the request), packages the signed files and attaches the
ZIP and checksum to a GitHub Release, using this version's section of
`CHANGELOG.md` as the release notes. A branch push alone never publishes
binaries.

`source.url` and `release.url` point to this WO4K repository and its latest
release. See [UPDATES.md](UPDATES.md) for accepted formats and preservation rules.
