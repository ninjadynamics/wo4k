# Beta release changes

## 0.1.0-beta.19

- Release Setup's icon file after loading it so Update can replace the icon
  while the window remains open. Keep native worker handles alive and honor
  successful completion receipts instead of treating missing exit codes as
  failures. Preserve specific failure explanations in the banner and operation
  line; keep exception chains and paths in diagnostic logs. Distinguish missing
  original source media from damaged installed game data. Place operation text
  above a thinner progress line.

## 0.1.0-beta.18

- Align HUD groups by visible artwork bounds to a uniform 180px inset at
  3840x2160, scaled with the interface at other resolutions. Center the native
  four-tile item indicator and its icons as one assembly; align the FPS counter
  to the same bottom inset. Account for transparent HD padding and SD fallback.

- Persist Setup sources/preferences and reconcile the installation before Play
  and when Setup opens. Import configured PS1 artwork/OST silently, verify
  installed outputs, repair damaged/missing data, synchronize runtime changes,
  and reuse healthy local assets with sources offline. Finish Update with a
  forced Install / repair even when already current. Cover manual ZIP updates
  and remove unchanged retired mod files using ownership snapshots, preserving
  custom artwork, saves and settings.

- Restore per-emitter PS1 additive explosions/flashes and subtractive smoke,
  including the original EXE's mode0 billboard path. Use one original-size
  quad per animation sample, retain interpolation, restore neutral128 colour
  modulation, and place these effects after opaque scenery. Emulate exact
  subtractive compositing with bounded D3D1 passes; retain native state and
  vertex-processing pointers. Cover actual native emission/closing code and
  overlapping, faded, black-backed effects in offscreen GPU regressions.
- Add a local texture picker with PS1-left/PC-right sliders, individual
  selectors, filtering, saved choices and complete JSON download. Show all
  484 mapped textures, including road reconstructions and byte-identical
  reuse; retain the explicit list of 151 disc-sourced PS1 images.
- Restore 128x128 roads from the independently paletted original tiles in PC
  WADs, retaining the richer PS1 colours without requiring a disc. Use the
  verified native race order when rebuilding all eight tracks' packed sheets.
- Add optional European PS1 texture import beside soundtrack selection.
  Prefer PS1 sources unless native bytes were verified identical at project
  time; install a fixed placement map, retain original TIM/STP source data,
  and preserve authored HD interface artwork. Data-only PS1 images do not
  enable or require CD audio. Retain sources across repair and incremental
  launch without runtime version/quality comparisons.
- Tighten filtered color-key silhouettes to half coverage, discarding dark
  black-matte fringe samples around native sprites while retaining masking.
- Add main-keyboard and numpad +/− controls for 3D shadow lift (0–300%) and
  Page Up/Page Down for highlights (0–300%, neutral at 100%). Show both values
  briefly and save Render/ShadowLiftPercent and Render/HighlightsPercent.
  Restrict adjustments to opaque 3D triangles after material preparation;
  preserve blended/shared vertices whose RGB also controls legacy opacity,
  along with menus, HUD and effects. Shadow lift 0% retains original lighting.
- Reset explicit alpha testing for native 3D execute buffers, including with
  neutral lighting, while preserving native color-key masking for foliage and
  billboards. Never disable color keying across the world pass.
  Insert resets after the original PROCESSVERTICES placeholder so its native
  closing routine retains a valid pointer; verify with the original x86 code.

## 0.1.0-beta.17

- Discover supported HD PNG replacements on each launch, including menu
  frames, controls, banner/logo artwork, reticles and native texture pages.
  Read authored PNGs directly and report filenames that cannot be mapped.
- Remove transparent-edge fringes with premultiplied-alpha rendering and join
  the menu logo into one draw. Match frame columns, cap edges, highlights and
  footer dividers to measured artwork pixels while retaining SD fallback.
- Bypass the original setup controls and event loop. Keep the game window
  hidden until graphics initialization completes.
- Add automatically saved Mono/Stereo/Dolby Surround and Borderless/Exclusive
  choices to Setup; carry selected modes through Install / repair.
- Extend offline asset/settings checks and native renderer coverage for
  startup, logo geometry and frame joins across five display shapes.

## 0.1.0-beta.16

- Record launch, setup, install and update stages and failures locally. Show a
  popup with the log path on failure, capture game exit codes and early
  PowerShell errors, and include minimal Windows/CPU/RAM/GPU support details.
  No device identifiers or automatic uploads. Keep Setup self-updatable.
- Play the intro between notice and title and support all four original ending
  triggers, with local MP4 conversion/replacements and original-video fallback.
  Hide the game cursor, use a real-clock title timer and controller-aware prompt,
  and present native title lettering before the display flip.
- Read HD PNG artwork directly from assets/hd without duplicate installed
  BMP/RGBA/PNG copies; migrate legacy layouts on every Play launch and retain
  custom artwork and video replacements.

## 0.1.0-beta.15

- Use one PC source field and extract included music automatically. Support
  data-only / small PC downloads with no music or optional PS1 music.
- Widen source buttons, add explicit source-status messages and restore
  music volume when repairing a silent install with a soundtrack.
- Add source-status and optional-music regression checks and credit the
  decompilation reference and compatibility/controller contributors.


## 0.1.0-beta.14

- Add the WO4K logo to the README and setup header, with a pre-sized app
  asset and DPI compensation. Keep the logo and Beta label, remove the
  redundant game title, and name WipeOut 2097 / XL in the description.

## 0.1.0-beta.13

- Separate source tracking from generated binaries. Add Makefile targets,
  optional local WinGet bootstrap and HOW-TO-COMPILE.md.
- Generate binary differences from text recipes; fetch checksum-pinned
  dependencies outside Git, including compatible libiconv 1.18.
- Package WO4K-named ZIPs and publish them through the version-tag workflow.
  A normal branch push remains source-only.

## 0.1.0-beta.12

- Name the mod WO4K in the README and refresh installation, controls, source,
  maintenance and release instructions against current behavior.
- Configure source.url for ninjadynamics/wo4k and release.url for its latest
  release, enabling the setup GitHub and Update controls.

## 0.1.0-beta.11

- Rename link configuration files to release.url and source.url throughout
  setup, the updater, release packaging and documentation. Their contents
  remain a single HTTPS URL; invalid or empty files disable their controls.

## 0.1.0-beta.11

- Embed the supplied red Ninja icon in Play.exe and cyan Ninja icon in
  Setup.exe, with seven sizes for Windows shell display. Use the cyan
  icon on the setup window as well.
- Preserve the original Ninja Dynamics logo and display it at native raster
  size with DPI compensation. Pre-resize GitHub to its final 105x24 pixels
  with ffmpeg instead of resizing its original image in the UI.

## 0.1.0-beta.9

- Match the setup layout with the supplied GitHub logo beside the subtitle
  and Ninja Dynamics artwork in the footer. Keep URL availability and
  tooltips on both logo elements.

## 0.1.0-beta.8

- Align the audio menu's Soundtrack label with Music/SFX and right-align
  PC/PlayStation with the volume bars. Adjust row spacing to the supplied
  layout, moving slider frames and knobs together with their labels.

## 0.1.0-beta.7

- Fix renderer initialization rejecting the race UI feature group because
  DrawShipTrails was already hooked by the refresh renderer. Extend the
  existing hook, so Full trails, backtick FPS and checkpoint flash timing
  activate together.
- Add an integration check that initializes the full renderer on a private
  image of the actual game EXE and invokes its patched race-start, effects
  and keyboard entry points. This reproduces the earlier activation failure.

## 0.1.0-beta.6

- Play runs the artwork and audio-effect installers before starting the game,
  refreshing changed or damaged outputs while skipping unchanged assets.
- Setup retains local source effects for future audio processing updates;
  source files never enter the release ZIP. Audio is never repeatedly denoised.
- Restore F1 camera switching; use backtick to toggle the FPS counter.

## 0.1.0-beta.5

- Default to the Full exhaust trails used by the original pacing test,
  independently of the clean install's simpler native setting. Remember
  off/simple/full choices made with F4.
- Present Extended Time from its live bonus timer on every render frame,
  using the clock-based native flash cadence rather than sparse physics frames.
- Enable the bottom-center FPS counter by default, with F1 to toggle and save
  its visibility. Keyboard camera switching moves to V; controller Circle
  continues to change the camera.

## 0.1.0-beta.4

- Center race announcements using their visible glyph widths, including
  Autopilot, Disengaging, Extended Time, Wrong Way, Perfect Lap and shield
  warnings. Keep the autopilot countdown and rear-lock label centered too.
- Preserve fractional text positioning, overlay heights, corner HUD padding
  and menu label positions across display aspect ratios.

## 0.1.0-beta.3

- Add background curl-based release updates configured by `release.url`,
  with package verification, file backups and rollback on failed copies.
- Add the Ninja Dynamics GitHub link configured by `source.url`.
  Missing, empty or invalid link files silently disable the relevant control.
- Describe the mod in the setup subtitle, with an expanded feature tooltip.
- Label setup WO4K / Beta and WipEout 2097 / XL.

## 0.1.0-beta.2

- Fix sound loading in a fresh installation: recognize the original game's
  doubled separators in relative asset paths, including `shp2shp.wav`.
- Rename the setup option to **Improve audio quality**.
- Use dark charcoal hover states for buttons, source fields, checkboxes,
  soundtrack choices and browse menus.
- Explain beside the soundtrack selector that banks can be switched during
  play in the game's Audio options. Both banks must be installed.

Existing installations only need the updated mod runtime; no audio extraction
or reprocessing is required. Saves, audio and preferences are preserved.
