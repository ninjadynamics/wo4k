<img src="assets/ui/wo4k-logo.png" alt="WO4K" width="480">

WO4K is a modern Windows mod for **WipEout 2097 / XL**, by Ninja Dynamics.
It adds widescreen rendering, smooth animation, extended track visibility,
HD interface artwork, modern controllers and local PC/PlayStation soundtracks
while retaining the original game's 30 Hz simulation and gameplay pace.

[Download releases](https://github.com/ninjadynamics/wo4k/releases) ·
[Source code](https://github.com/ninjadynamics/wo4k) ·
[Report a bug](https://github.com/ninjadynamics/wo4k/issues) ·
[Verifying downloads](#verifying-downloads)

This repository contains the mod's C source code, artwork, Makefile and patch
recipes. Compiled EXEs/DLLs and other generated build outputs are excluded from
Git. Ready-to-use ZIP packages and their SHA-256 checksums belong in
**Releases**. To play, download a release ZIP rather than GitHub's automatic
“Source code” archive.

## Features

- Any-aspect-ratio rendering, corner-aligned HUD and adapted menu layouts.
- Rendering at high refresh rates, interpolated animation, crossfaded sprite
  animation and smooth menus, menu models and credits.
- Extended track visibility and corrected effect placement/transparency.
- PS1 blend choices for shared explosions/flashes (additive) and smoke
  (subtractive), with original sprite silhouettes and smooth animation.
- During a race, press **+ / −** (main keyboard or numpad) to adjust shadow
  lift in the 3D lighting. Each tap changes it by 5%, from 0% (original) to
  300%. **Page Up / Page Down** adjusts highlights from 0–300%, with 100%
  retaining their original level. A brief readout shows both saved values.
  Menus, HUD and blended materials retain their original colors/transparency.
- HD fonts, HUD, notice and title artwork, with original SD assets retained.
- Restore full-colour road materials from original tiles already in the PC
  data. An optional European PS1 disc adds original texture/sprite sources;
  selection follows a fixed, project-audited map with byte-identical PC reuse.
- SDL3 gamepad support, including DualSense over USB or Bluetooth.
- PC music plus an optional PlayStation soundtrack, switchable during play.
- Local sound-effect noise reduction at the original 22.05 kHz sample rate,
  with volume compensation.
- Portable installation: no disc mounting, fixed drive path or registry setup.

## Requirements

**Windows 10/11 x64** and your own supported **Windows PC game files** are
required. Supply one PC ZIP/disc image, or the small **11 MB PC version**.
Setup extracts any PC music included with that source automatically.

Music is optional. The small version can run with sound effects only, or with
an optional PlayStation soundtrack. The European PS1 disc (SLES-00327) also
supplies original artwork. A data-only ISO works for artwork without music;
PS1 data cannot replace the Windows executable. There is no separate PC
soundtrack field. See [native texture selection](docs/NATIVE-TEXTURES.md).

Setup accepts game folders, ZIP archives and supported ISO, MDS/MDF or CUE/BIN
images. Keep paired disc files together. Data-only ISOs and small PC downloads
contain no CD audio; choose **No music**, or add a PlayStation soundtrack.
Supported executable checksums are listed in [manifests/patches.json](manifests/patches.json);
unsupported binaries are rejected before an installation is committed.

## Install and play

1. Download a ZIP from [Releases](https://github.com/ninjadynamics/wo4k/releases)
   and extract it into a writable folder outside Program Files.
2. Open **Setup.exe**. It searches common local folders in the background.
   Use the browse button to select a file/folder, drop one onto the window, or
   use the animated ✨ button to search again. No match leaves a field silently
   empty. Hover over settings
   labels for explanations.
3. Select the PC game source once. Optionally add a PlayStation game source,
   then choose available PC/PlayStation music or **No music**.
4. Choose **Audio output** (Mono, Stereo or Dolby Surround),
   **Fullscreen mode** (Borderless or Exclusive), **Install HD artwork**
   and **Improve audio quality** as desired,
   then click **Install**.
5. Click **Play**, or run **Play.exe**. The original display-selection screen
   is bypassed; WO4K renders at your display's resolution and aspect ratio.

After installation, Setup hides the source selectors and shows **Update**,
**Repair** and **Play**. **Repair** reveals the saved PC/optional PlayStation
sources for the current session. **Repair** becomes a yellow **Cancel**, and
**Play** becomes **Install**. Review the paths and click **Install** to rebuild
the installation, or **Cancel** to discard edits and return to Play.
Closing and reopening Setup hides the source selectors again.

Change **Audio output** in Setup anytime. It saves automatically for the next
launch without reinstalling; Stereo is the default.
**Fullscreen mode** also saves for the next launch without reinstalling.
Borderless is the default and fills the display without a window border;
Exclusive uses exclusive fullscreen presentation.

Change soundtracks on the fly in **Options → Audio** when both banks are
installed. Your choice is saved. The PC bank contains nine songs; the optional
PlayStation bank contains eleven.

Before every launch, Play checks the configured installation, synchronizes
updated runtime files, imports missing PS1 artwork/music, refreshes artwork and
sound effects, and repairs missing or damaged outputs. Setup runs the same check
when opened. The check takes a few milliseconds: like Git, WO4K remembers each
verified file's size, timestamps and file ID with its SHA-256 (in
`.state/integrity.json`), and reads a file again whenever any of them changed,
so edited, replaced, truncated or missing files are still found and repaired.
The first launch after an install or update verifies everything once, and
**Repair** always verifies every file. Edit HD PNGs in `assets/hd/` to have them
picked up at the next launch; audio outputs are rebuilt when changed or
damaged, and HD PNGs are validated and read directly by the renderer. Setup's
HD/audio preferences are respected. Original effects stay locally in
`wobble/audio/source/`, so refresh never repeatedly denoises its own output or
needs the disc again. Native road/PS1 improvements are built into the packed SD
sheets during setup and retained locally. Setup saves source paths and
preferences in `.state/setup.json`. Healthy installed assets and cached PS1
artwork remain usable when the original disc is offline; restoring missing
music or PC data may require the configured source again. Texture choices are
fixed at project time; launch checks verify installed output integrity without
comparing PC and PS1 quality.
Play checks every PNG in `assets/hd/` before each launch. All eleven
exported UI filenames are supported: `font-and-numbers`, `hud-gauges`,
`hud-reticles`, `menu-background`, `menu-frame-left/middle/right`,
`menu-controls-1/2`, `menu-logo-atlas`, and `menu-banner-logo` (all `.png`).
`notice.png` and `title.png` retain their startup layouts.

Individual texture edits are matched **by filename only** against
`manifests/hd-textures.json`, which covers all 576 usable exported textures.
Drop a matching PNG into `assets/hd/` and Play automatically registers it on
the next launch; no executable rebuild is needed. Resolution and aspect ratio
are up to you, and source PNGs stay unchanged. The renderer fits the image into
the original texture region. Removing the PNG restores the native artwork on
the next launch. The three exported `screen0` screenshot references are not
runtime texture slots.

Additional native TGA replacements work without a rebuild: use the original
name, such as `SKY01.png`, or `HUD2.page0.png` for a zero-based individual page.
A whole multi-page replacement such as `HUD2.png` retains the original
horizontal page strip. Native filenames can also mirror `Pics/Hi-res/` under
`assets/hd/`. Exported semantic names take priority over generic native names;
`menu-banner-logo.png` overlays only the logo regions of `menu-logo-atlas.png`,
keeping the atlas's buttons/panels (or their SD originals).
The HD header is joined in memory and drawn as one rectangle, allowing smooth
filtering across the original atlas split without a visible seam.

Keep each exported atlas's element positions to preserve its layout. PNG
dimensions need not be square, power-of-two or an exact multiple of the original.
The renderer manages GPU storage and fits the source to the original UV layout.
Unmapped or invalid PNGs produce a clear error naming the file, rather than
silently being skipped. Restart Play after changing artwork. The renderer uses
premultiplied color for filtering/blending, preserving smooth edges and fades;
your PNG files stay unchanged.

HD PNGs live only in assets/hd; the runtime reads them directly, with no BMP,
RGBA or PNG copies in Wobble. Play applies layout migrations and removes known
obsolete mod outputs before every launch, including after Update. Custom files,
saves and preferences are preserved. Close the game before changing assets.

## Videos

Setup extracts all five original PC videos to `wobble/movies/` when your source
includes them. Startup runs in this order: **launcher → notice → intro → title**.
The intro player is prepared while the notice shows, so the movie follows it
at once.
The four endings play on their original completion events: six gold medals
(XTRO1), the Phantom challenge (XTRO2), eight gold medals (XTRO3), and the Piranha
challenge (XTRO4). Original achievement and save handling stays in the game.

Drop in `intro.mp4` (or `WXLINTR.mp4`), `XTRO1.mp4`, `XTRO2.mp4`, `XTRO3.mp4`,
or `XTRO4.mp4` to replace any video, including with HD artwork. MP4 takes priority;
the corresponding original `WXLINTR.MPG` / `XTRO1.MPG`–`XTRO4.MPG` is the fallback,
including when Windows cannot decode a replacement. Setup preserves your MP4
replacements. WO4K's own player (`wo4k-video.exe`) plays the original MPEGs
with Windows' built-in decoders; no conversion, encoder or codec pack is needed.
Videos fit the screen without stretching. Skip with **Esc**,
**Enter**, **Space**, or a controller button. The cursor stays hidden during
videos and in-game, and movies have no tooltip. Music pauses during cutscenes.

The small PC download works without movies; missing files or unavailable Windows
media codecs do not block the game. Movie files are supplied by you and never
included in the mod release. Run **Install / repair** with your PC disc/ZIP to
add movies to an existing installation. No disc needs to stay mounted afterward.

The title-screen attract timer uses real elapsed time: 30 seconds on the first
visit and 5 seconds on later visits. When a controller is detected, the prompt
changes to **Press start**; Start/Options continues, as does Cross. Keyboard
users retain **Press enter**.

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
| Options / Start | Continue from title / pause in game |
| R1 + Square | Fire a rear-locked missile |
| Backtick (`) | Toggle the bottom-center FPS counter |
| F4 | Cycle exhaust trails: off / simple / full |

Full trails are the default. FPS visibility and trail selection are remembered.

## Updates and links

The **GitHub** logo opens this source repository. **Update** downloads a newer
release over HTTPS with Windows' built-in networking, verifies it, backs up
changed files and preserves installed game data, audio, saves, preferences and
URL configuration. When the installed copy is signed, an update's binaries must
be signed by the same publisher. Update finishes with **Install / repair**,
even when the latest release is already installed. Downloading a release ZIP and
overwriting the mod folder is also supported: the next Play or Setup launch
reconciles the installation locally. Both launchers remove retired, unchanged
mod-owned files and empty directories using the previous release inventory.
Modified/custom files and saves are kept. Close the game before updating. Setup
reopens automatically after the update and Install / repair complete successfully.
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

The package contains Setup, Play, the mod runtime, checksum-guarded executable
patch recipes, supplied HD artwork and licensed compatibility/controller
dependencies. It contains no scripts, and no original game executable, game
data, music or sound effects. Setup reads your source files without modifying
them and patches your executable locally. Installation needs no network,
administrator access, external installer or other software. The optional
Update action downloads mod releases.

```text
Setup.exe, Play.exe          setup window and game launcher
assets/hd/                   editable supplied HD PNGs
runtime/                     mod runtime, controller host, video player, DDrawCompat
patches/                     checksum-guarded executable patch recipes
manifests/, licenses/        file catalogues, provenance and third-party licenses
wobble/                      created locally by Setup
  Wipeout2097.exe            patched from your supplied PC executable
  music/pc/                  PC soundtrack
  music/psx/                 optional PlayStation soundtrack
  audio/                     sound effects; source/ retains original inputs
  textures/sd/               original artwork/texture containers
```

Repairs stage an installation before replacing it and preserve saves and
preferences. WO4K keeps its disk use bounded: `backups/` holds the most recent
previous game installation (without its music, which Repair rebuilds from your
sources) and the three most recent update backups. Successful install sessions
are removed from `.work/` straight away, other leftovers there after 7 days,
and logs after 30 days. Installed game files, private work, logs, backups,
exports and `dist/` release artifacts are excluded from Git. Keep the mod
folder together when moving it.

## Source, tools and builds

Everything is written in C: Setup, Play, the installer, updater, format
readers, audio processor, video player and game runtime. Setup draws its window
with [raylib](https://www.raylib.com/). No PowerShell, .NET or Python is needed
to build, install or play.

- [Advanced use and maintainer tools](docs/TOOLS.md)
- [How to compile](HOW-TO-COMPILE.md)
- [Build dependencies and release assembly](docs/BUILD.md)
- [Updater configuration and release publishing](docs/UPDATES.md)
- [Changelog](docs/CHANGELOG.md)

Build with `make` and a MinGW-w64 toolchain. `make release` builds, tests and
packages the distributable ZIP, checksum and file inventory under `dist/`.
Pushing a matching version tag triggers the GitHub Actions release workflow,
which publishes the release; a normal branch push builds and tests only. GitHub's generated source archives are not update packages.

Every launch, installation and update writes a local diagnostic log in `logs/`.
If an operation fails, a popup shows the exact log location; launch failures
include the Windows error or game exit code. If the mod folder is unwritable,
the popup points to a fallback log under `%TEMP%/WO4K`.

Support logs include Windows edition/build/architecture, CPU model and core
counts, RAM capacity, GPU model/driver, and the mod version and startup stages.
They do not query machine/account names, serial numbers, UUIDs, MAC/IP addresses
or hardware device IDs. Common personal paths are redacted. Logs stay local and
are never uploaded automatically. Review them before sharing.

For a game crash, share the **main `play-*.log` shown in the popup**. It includes
current-session tails of graphics, native exception and video logs; the complete
attachments share its filename prefix. Play records the game's exit code and peak
memory use, the size and signature status of each runtime file and the relevant
settings, and checks Windows Application Error events for the same child process. Native fault reports distinguish first-chance
observations (which may be handled) from unhandled exceptions and record the
instruction's module/offset, access type/address, registers and bounded code-address
candidates. No memory dump is created. These diagnostics provide evidence for
investigation; an access violation alone does not establish a RAM/video problem.
The release's `runtime/wohr.map` maps runtime addresses back to its exact build.
The main report and each saved attachment identify the mod version; native crash
logs record the version compiled into the DLL, including before graphics starts.

Existing players on 0.1.0-beta.24 or earlier: the old updater cannot install this
release, so download the ZIP once and extract it over your mod folder, replacing
files. Then open Setup or Play; it repairs the installation, keeps your saves
and preferences, and removes the retired PowerShell files. Later updates work
through **Update** again. Logging is automatic; there is no diagnostic option
to enable.

WO4K is a beta. Report the mod version, Windows version, source type, symptoms
and relevant `logs/` through [Issues](https://github.com/ninjadynamics/wo4k/issues).

## Uninstall

WO4K is portable. To remove it, close the game and Setup, then delete the WO4K
folder. That folder holds the mod, the installed game copy (`wobble/`), saves,
settings, logs and backups. WO4K writes no registry entries, installs nothing
system-wide and changes no Windows settings. The only file it may create
outside its folder is a diagnostic log in `%TEMP%\WO4K` when the mod folder is
not writable; delete that folder too if it exists. Your original game sources
are only read, never modified.

## Privacy

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it.

WO4K contacts the network only when you click **Update** (it downloads release
information and the release package from GitHub) or the **GitHub** logo (it
opens the project page in your browser). Diagnostic logs stay on your PC and
are never uploaded; you decide whether to share them.

## Verifying downloads

Release binaries are built from the tagged source by this repository's GitHub
Actions release workflow, after its tests and fuzzers pass. They are not
code-signed at the moment, so Windows SmartScreen may warn the first time you
open Setup or Play; choose **More info → Run anyway** if the download came from
this repository's Releases page. Each release publishes a `.zip.sha256`
checksum beside the ZIP. To compare it, run
`certutil -hashfile wo4k-<version>.zip SHA256` in the download folder.

`Setup.exe`, `Play.exe`, `runtime/wohr.dll`, `runtime/wo2097-controller.exe` and
`runtime/wo4k-video.exe` are built from this repository. The third-party
DDrawCompat `runtime/ddraw.dll` and GNU libiconv `runtime/libiconv-2.dll` are
included unmodified, as published by their upstream projects.

## Acknowledgements

Huge thanks to the people who made this work possible:

- **Yash-Singh1 and contributors — [wipeout-xl-decomp](https://github.com/Yash-Singh1/wipeout-xl-decomp).**
  This repository covers WipEout XL / 2097 on PlayStation and includes a partial
  PC decompilation. Its PC source was a valuable reverse-engineering reference
  for locating and understanding the original game's routines while building WO4K.
- **[DDrawCompat](https://github.com/narzoul/DDrawCompat), by narzoul and contributors,**
  for the compatibility layer that makes the original rendering APIs usable on
  modern Windows.
- **[SDL3](https://github.com/libsdl-org/SDL) and its contributors,** including
  its HIDAPI integration, for modern controller support.
- **[raylib](https://github.com/raysan5/raylib), by Ramon Santamaria and
  contributors,** for the Setup window, and **[miniz](https://github.com/richgel999/miniz)**
  for ZIP handling.
- The original **Psygnosis developers, artists, musicians and designers**, and
  the WipEout community keeping these games alive.

Third-party licensing and artwork attribution remain documented in
[licenses/](licenses/) and [manifests/](manifests/).

Our contributions are MIT licensed; see [LICENSE](LICENSE). Artwork provenance
and third-party licenses are recorded in [manifests/](manifests/) and
[licenses/](licenses/). WipEout is credited to its original creators.
WO4K is an independent mod.
