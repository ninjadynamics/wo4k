# WO4K

WO4K is a modern Windows mod for **WipEout 2097 / XL**, by Ninja Dynamics.
It adds widescreen rendering, smooth animation, extended track visibility,
HD interface artwork, modern controllers and local PC/PlayStation soundtracks
while retaining the original game's 30 Hz simulation and gameplay pace.

[Download releases](https://github.com/ninjadynamics/wo4k/releases) ·
[Source code](https://github.com/ninjadynamics/wo4k) ·
[Report a bug](https://github.com/ninjadynamics/wo4k/issues)

This repository contains the mod's source code, artwork, Makefile and installer
scripts. Compiled EXEs/DLLs and other generated build outputs are excluded from
Git. Ready-to-use ZIP packages and their SHA-256 checksums belong in
**Releases**. To play, download a release ZIP rather than GitHub's automatic
“Source code” archive.

## Features

- Any-aspect-ratio rendering, corner-aligned HUD and adapted menu layouts.
- Rendering at high refresh rates, interpolated animation, crossfaded sprite
  animation and smooth menus, menu models and credits.
- Extended track visibility and corrected effect placement/transparency.
- HD fonts, HUD, notice and title artwork, with original SD assets retained.
- SDL3 gamepad support, including DualSense over USB or Bluetooth.
- PC music plus an optional PlayStation soundtrack, switchable during play.
- Local sound-effect noise reduction at the original 22.05 kHz sample rate,
  with volume compensation.
- Portable installation: no disc mounting, fixed drive path or registry setup.

## Requirements

**Windows 10/11 x64** and your own supported **Windows PC game files and PC
soundtrack** are required. A PlayStation disc is optional and supplies music
only; it cannot replace the PC game or PC soundtrack.

Setup accepts game folders, ZIP archives and supported ISO, MDS/MDF or CUE/BIN
images. Keep paired disc files together. A data-only ISO contains no CD audio:
provide an audio-bearing PC disc image or an existing extracted PC music folder
separately. A game source that already includes PC music can supply both fields.
Supported executable checksums are listed in [manifests/patches.json](manifests/patches.json);
unsupported binaries are rejected before an installation is committed.

## Install and play

1. Download a ZIP from [Releases](https://github.com/ninjadynamics/wo4k/releases)
   and extract it into a writable folder outside Program Files.
2. Open **Setup.exe**. It searches common local folders in the background.
   Use the browse button to select a file/folder or the animated ✨ button to
   search again. No match leaves a field silently empty; controls have tooltips.
3. Supply the Windows game and PC soundtrack. Optionally add a PlayStation
   soundtrack source and choose the default music bank.
4. Choose **Install HD artwork** and **Improve audio quality** as desired,
   then click **Install / repair**.
5. Click **Play**, or run **Play.exe**. In the original display-selection
   dialog choose **Direct3D HAL / 16K colours / 640x480**. WO4K renders at your
   display's resolution and aspect ratio.

Change soundtracks on the fly in **Options → Audio** when both banks are
installed. Your choice is saved. The PC bank contains nine songs; the optional
PlayStation bank contains eleven.

Every Play launch runs the artwork and sound-effect installers first. Edit HD
PNGs in `assets/hd/` to have them picked up at the next launch. Changed or damaged
outputs are rebuilt; unchanged files are verified and skipped. Setup's HD/audio
preferences are respected. Original effects stay locally in `wobble/audio/source/`,
so refresh never repeatedly denoises its own output or needs the disc again.
Close the game before changing installed assets.

## Controls

PlayStation button names below describe SDL3's corresponding controller positions.
Keyboard input remains available.

| Control | Action |
| --- | --- |
| Cross | Accelerate / confirm |
| Square | Fire / go back in menus |
| Left stick | Turn / pitch |
| L2 / R2 | Left / right analog air brake |
| D-pad | Menu navigation |
| Triangle | Discard weapon |
| Circle / F1 | Change camera |
| Options / Start | Pause |
| R1 + Square | Fire a rear-locked missile |
| Backtick (`) | Toggle the bottom-center FPS counter |
| F4 | Cycle exhaust trails: off / simple / full |

Full trails are the default. FPS visibility and trail selection are remembered.

## Updates and links

The **GitHub** logo opens this source repository. **Update** uses Windows
`curl.exe` to download and verify a newer release, backing up changed files and
preserving installed game data, audio, saves, preferences and URL configuration.
Close the game before updating; reopen Setup afterward to load interface changes.
It does not check for updates or download anything automatically at startup.

The supplied plain-text configuration files contain one HTTPS URL each:

| File | Default |
| --- | --- |
| `source.url` | https://github.com/ninjadynamics/wo4k |
| `release.url` | https://github.com/ninjadynamics/wo4k/releases/latest |

Missing, empty, unreadable or invalid files silently disable their controls.
The configured latest-release endpoint follows GitHub's latest full release;
GitHub prereleases are available manually on the Releases page. See
[release/update details](docs/UPDATES.md) for verification and publishing rules.

## Files and local data

The package contains the mod's code/runtime, checksum-guarded executable binary
differences, supplied HD artwork and licensed compatibility/controller dependencies.
It does not contain an original game executable, game data, music or sound effects.
Setup reads your source files without modifying them and patches your executable
locally. Installation needs no network, administrator access, external installer,
Python or FFmpeg. The optional Update action downloads mod releases.

```text
assets/hd/                   editable supplied HD PNGs
scripts/                     PowerShell setup, extraction, install and build tools
src/                         C runtime, controller, audio and launcher source
runtime/                     built mod runtime and dependencies
patches/                     checksum-guarded binary differences
wobble/                      created locally by Setup
  Wipeout2097.exe             patched from your supplied PC executable
  music/pc/                  PC soundtrack
  music/psx/                 optional PlayStation soundtrack
  audio/                     sound effects; source/ retains original inputs
  textures/sd/               original artwork/texture containers
  textures/hd/               HD PNGs and generated runtime uploads
```

Repairs stage an installation before replacing it, preserve saves/preferences and
retain the previous installation in `backups/`. Successful extraction sessions
are cleaned; failed sessions in `.work/` and `logs/` help diagnose problems.
Installed game files, private work, logs, backups, exports and `dist/` release
artifacts are excluded from Git. Keep the mod folder together when moving it.

## Source, tools and builds

All setup, extraction, installation, build and verification scripts are `.ps1`.
Setup.exe and Play.exe are thin launchers for Windows PowerShell 5.1. The audio
processor is included as C source and processes effects locally during setup.

- [Standalone extraction and installation commands](docs/TOOLS.md)
- [How to compile: Makefile or optional local WinGet bootstrap](HOW-TO-COMPILE.md)
- [Build dependencies and release assembly](docs/BUILD.md)
- [Updater configuration and release publishing](docs/UPDATES.md)
- [Changelog](docs/CHANGELOG.md)

Use `make` with an existing toolchain, or optional `.\compile.ps1` to fetch
missing compilers/Make into `.tools/` and run Make. `make release` builds,
tests and packages. Pushing a matching version tag triggers the GitHub Actions
release workflow; a normal branch push updates source only.

Build scripts write the distributable ZIP, checksum and file inventory under
`dist/`. Publish the ZIP and its `.zip.sha256` sidecar together in this repository's
Releases; GitHub's generated source archives are not update packages.

WO4K is a beta. Report the mod version, Windows version, source type, symptoms
and relevant `logs/` through [Issues](https://github.com/ninjadynamics/wo4k/issues).

Our contributions are MIT licensed; see [LICENSE](LICENSE). Artwork provenance
and third-party licenses are recorded in [manifests/](manifests/) and
[licenses/](licenses/). WipEout is credited to its original creators.
WO4K is an independent mod.