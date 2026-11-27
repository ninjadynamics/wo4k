# Advanced use and maintainer tools

Players need only Setup and Play. Everything the old standalone scripts did
(extracting music, effects and textures, applying the executable patch,
refreshing assets) is built into them.

## Play options

```bat
Play.exe --refresh-only       :: check and repair the installation, then exit
Play.exe --no-error-dialog    :: report failures only in logs\
Play.exe --root <folder>      :: use the installation in another mod folder
Play.exe --purge              :: delete every log, crash logs included, and exit
```

Setting the environment variable `WO2097_VIDEO_PREPARE=0` before starting Play
turns off preparing the intro player during the notice screen; the player is
then started when the intro begins, as in earlier releases.

Setting `WO2097_VIDEO_IN_GAME=1` before starting Play makes the game present
the movies itself: the movie player opens no window (`wo4k-video.exe --frames`)
and hands each decoded frame to the game through shared memory. Exclusive
fullscreen needed this; that mode is currently switched off
(`WO_EXCLUSIVE_FULLSCREEN_OFFERED` in `src/core/wo_settings.h`).

## Installing from the command line

There is no separate command-line installer; use Setup. PC game data alone is
enough for the minimum install, and missing PC music is optional. An optional
PlayStation source can supply music (and artwork) for a data-only PC install;
it never replaces PC game data. Choose **No music** for sound effects only.

Close the game before modifying installed artwork, music or effects. Keep your
original inputs.

## HD artwork

HD PNG sources live in `assets/auth/` (authored, shipped) and `assets/hd/` (upscaled sets, used
where `auth` has no PNG of that name); the runtime decodes them directly.
Play validates them before each launch. Setup's HD/original-audio preferences
are retained during every refresh. Setup keeps the original sound effects in
`wobble/audio/source/`; these are locally supplied game files, never release
payloads. Changes to the audio processor are detected and the effects rebuilt.

In a source checkout, double-click `assets/auth/trim.cmd` to losslessly
recompress the PNGs beside it. It runs `deband -z` (see below; build it once
with `mingw32-make deband`), which keeps every pixel, alpha value, bit depth
and colour-profile chunk, removes editing metadata, verifies the result by
decoding it, and replaces a file only when it is smaller and unchanged on disk
meanwhile. Animated PNGs are skipped. Use `--no-pause` from a terminal to
suppress the final keypress prompt. The launcher is kept in Git and excluded
from game release ZIPs.

## Maintainer commands

`wo4k-build` (built by `make tools` as `build\tools\wo4k-build.exe`) reads game
files you own and never modifies them:

```bat
:: SD texture containers plus PNG exports of every TGA, TIM and CMP frame
build\tools\wo4k-build.exe export-textures "D:\Games\Wobble" exports\sd

:: One soundtrack as trackNN.wav plus tracks.ini (pc or psx)
build\tools\wo4k-build.exe extract-music "D:\Discs\Wipeout.XL.mds" exports\pc pc
```

`export-textures` accepts an original PC game folder or an installed `wobble`
folder. PNGs go to `<output>\exports`; CMP frames are numbered by their original
order under `<name>.cmp.png.parts\`. SD PNG exports are painting references,
with native loader row orientation. Native track WAD archives remain with their
geometry. No automatic texture resynthesis or recompression occurs.

`extract-music` accepts a music folder or an MDS/MDF, CUE/BIN image; unpack a
ZIP first.

## Maintainer texture debander and PNG trimmer

The user-supplied C debanding utility is in `tools/deband/`, together with the
lossless PNG trimmer. Build it from the repository root:

```bat
mingw32-make deband         :: tools\deband\deband.exe (fetches pinned libpng, libjpeg, zlib)
mingw32-make deband-test    :: trimmer tests
tools\deband\deband.exe -z assets\auth
```

`-h` shows the available options. Use `-o` with a separate output directory to
retain the source images. FFmpeg is preferred for debanding when installed,
with a native C fallback; trimming needs no FFmpeg. The filter itself
(`tools/deband/filter.c`) is also built into Setup and Play. Debanded PNG output is
trimmed automatically. See [the tool's README](../tools/deband/README.md) and
[the local notes](../tools/deband/LOCAL.md). The executable and processed
artwork remain ignored, and this maintainer tool is excluded from game
releases.

`tools/Deband-Ps1Textures.py --disc <PlayStation ZIP or .bin>` exports every
texture frame on a PlayStation disc to `assets/sd-original/` (see-through
texels transparent), debands the set into `assets/sd-debanded/` and builds a
left/right comparison page in `exports/ps1-deband-comparison/`. Scenery
frames whose borders meet in the track models are debanded with a margin of
their neighbours, so a gradient crossing two frames gets one ramp. Both
folders are generated and ignored. The page exports the choices as JSON:
saved as `manifests/deband-selection.json`, they are what Setup and Play
deband the game's textures by (see
[the texture notes](NATIVE-TEXTURES.md#debanded-textures)). `assets/sd/` is
that step's output, not this tool's.

## Authoring studio

`tools\auth.cmd` (`wo4k-build auth`, `src/tools/build_auth.c`) opens a page
for drawing textures anew, served from this machine only while its window
is open. Each card is one picture with every copy of it in the game: the
same texture is there under several names, at one size or several (a road
on every track, a sign at 64 texels here and 128 there). The copies are
found at every start from `wobble/textures/originals/` (the same picture
exactly, or the same picture in the same proportions at another size;
pictures of one size that are only alike, an animation's frames, stay
apart) and written to
`manifests/auth-masters.json`, which Play reads: the one authored file of a
group is shown in the places of all of it, each at its own size, and a copy
with a file of its own keeps that.

On the page, **Author** puts a picture to start from into `assets/auth`
(the upscaled one where there is one, else the game's own), **Edit** opens
it in the program that opens PNGs (Photoshop, say), **Show in folder**
selects it in Explorer, and **Stop authoring** moves it to
`assets/auth-unused/` (never deleted; the HD fonts, HUD and menus, which the
game has no picture of its own for, cannot be stopped). Where a group has
more than one authored file, **use this** keeps one. A name that is not
really the same picture is taken out of its group with ✕ and stays out
(the manifest's `"apart"`) until ↺ puts it back. The page follows the
folder by itself, so a save in the editor shows on it at once; the game
shows it the next time it starts. Requests need the token the page was
opened with.

### Skies

`tools\sky.cmd` (`wo4k-build sky`, `src/tools/build_sky.c`) opens the
studio at its sky page. The game repeats each track's one sky picture round
the sky, eight or twelve times (and twice up the sky on Talon's Reach), so a
sky could only be drawn as that narrow picture. On the page, pick a track
and drop any picture on it (PNG, JPG or anything else Windows reads; at most
8192 pixels each way, larger ones are made smaller in the browser). It
becomes the track's authored picture, as PNG (a picture already there goes
to `assets/auth-unused/`), laid once round the sky, undistorted, its middle
on the horizon. Then the sliders:

- **The picture is**: a photo or painting (the default, unless it has a
  panorama's 2 : 1 proportions), laid flat on the sky's faces as on walls
  round you, so straight lines stay straight; or a 360° panorama, laid by
  direction from the camera, as panoramas are made. The 1996 skies are
  low-polygon: Talon's Reach is a box of four flat walls, Gare d'Europa,
  Odessa Keys and Spilskinanke rings of 16 flat panels, the others domes.
  On Talon's Reach a photo four times round fills each wall as a 90 degree
  photo would.
- **Repeats**: how many times it goes round (the counts offered are those
  whose seams fall between the sky's faces).
- **Turn**: where its seam is (the places a seam fits).
- **Up / down**: where it sits.
- **Stretch**: taller or flatter (it notches at none).

Each change shows in the view at once and is saved when the slider is let
go. Sky beyond the picture's top or bottom edge shows its edge row
stretched, and a picture reaching beyond the sky has those parts unseen. An
edge between the sky's rows of corners bends the picture a little in the
faces it crosses, so moving it, an edge within a degree or so of a row goes
onto it. The view shows the sky from the camera's place, yours or the
game's; drag to look round (the sky stays under the pointer), the wheel
zooms; the view is 90 degrees across, about a racing camera's, and
shows the sky's faces where the model has them. Under **More**: replace the picture, open it in your editor, show it
in its folder, or go back to the game's sky (the picture goes to
`assets/auth-unused/`). A track with no picture can also start from the
game's own sky, unwrapped once round into `assets/auth` to draw over. A
picture authored before the page (laid out the game's way) stays so until
**Lay it out my way**.

The layouts are kept in `manifests/sky-layout.json`; Play lays a track's sky
out by its entry while the picture is authored and Install HD artwork is on,
by rewriting the texture coordinates of the sky's faces in the track's
archive (`src/core/wo_sky.c`). The
sky's faces stay as they are, so the picture's columns are whole texels of
the 128-texel sky picture: a column's edges may be half a texel off, and the
last texel column and row of the picture are not shown.

## Maintainer neural upscaler

Setup's **Upscale textures** makes this set on the player's computer
(`src/core/wo_upscaled.c`, at 4x, when Play next starts). The tool is the
same code for maintainers: `tools\neural-upscale.cmd` (or `wo4k-build upscale
wobble\textures\originals assets\hd 4 assets\auth`) makes a whole set of
upscaled textures in `assets/hd/` from the textures as the game has them, at
2x or 4x: about 550 pictures in under half a minute. Those are in
`wobble/textures/originals/`, which the deband step fills with every picture
its choices name, whatever the choice: upscaling does not depend on
debanding. It leaves out the pictures `assets/auth/`
has and Valparaiso's waterfall. `assets/hd/upscaled.json` records what was
made from which picture, so only changed pictures are made again, and
unticking the option removes them. Play uses `assets/hd/` only while the
option is ticked.

Each picture is upscaled, then debanded: at four times the size a
gradient's steps of one level are four pixels wide. So the upscaled picture
goes through the four-sample filter and the dither, reaching as far across
the texture as at its own size. The band ramps are not made again; they are for the
PlayStation's flat bands of one exact colour.

The network is Rich Geldreich's
[neural_upsampler](https://github.com/richgel999/neural_upsampler) (public
domain): 11,948 weights that read the 7x7 pixels around each pixel and give
the 2x2 that replace it. Its weights are vendored in
`third_party/neural_upsampler/`; `src/core/wo_upscale.c` runs it in C and a
test holds it to the library's own output byte for byte. The network was
trained to undo one particular softening, which a PlayStation texture does
not have, so each texture is first filtered into what the network expects
(the header explains). The result is not invented detail: edges come out
straight where the texels stepped, and the picture is sharper than the
game's own bilinear filtering makes it. A 4x set makes every page of
textures 8 MB of video memory (2 MB at 2x).

## Maintainer billboard sheet

`wo4k-build billboards wobble exports/billboards assets` writes a contact
sheet of the trackside billboards (`exports/billboards/index.html`, open it in
a browser): every board of the installed track archives with its picture as
the game shows it, its frame or backing behind it, and how much the picture
is stretched across. Each board takes a choice: keep, fit vertically (bars in
a colour picked from the picture), tile from the centre, stretch by a
percentage of the picture's own proportions, or not a billboard. Choices are
kept in the browser and exported as JSON. `tools\billboards.cmd` makes the
sheet again from the current textures and opens it: run it after adding or
changing artwork in `assets\auth`, which shows on the page at once, without
starting the game first. `wo4k-build
billboard-layout <export.json> manifests/billboard-layout.json` turns an
export into the manifest Setup and Play lay boards out by
(`src/core/wo_billboards.h`); the sheet itself always shows the boards as
they were before any layout, which is what the manifest's face numbers
refer to. The page is
`tools/billboards/index.html`; how boards are found is in
`src/tools/build_billboards.c`.

## Maintainer lighting preview

Setup's "Adjust shadows / highlights" dialog previews the settings on three
pictures in `assets/ui/` (`light-preview-bare.png`, `-full.png` and
`-light.png`). To make them, run `tools\capture-light-preview.cmd`, race, and
press F9 at the view to show: the scene holds still while three screenshots
are taken (lit as the game lights it, fully lit, and unlit), then the race
runs on. `python tools/Build-LightPreview.py` turns the three newest
screenshots into the pictures. F9 does this only when the game is started
through that script.
