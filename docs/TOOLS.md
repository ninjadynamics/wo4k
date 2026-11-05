# Standalone tools

Run in Windows PowerShell5.1 from the mod root. No Python/FFmpeg,
mounting tools or separate image decoder is needed.

```powershell
# PC music; for PlayStation use -Bank Psx and its ZIP/CUE/BIN source.
.\scripts\Extract-Music.ps1 -SourcePath 'D:\Discs\Wipeout.XL.mds' -Destination '.\exports\pc' -Bank Pc
.\scripts\Install-Music.ps1 -SourcePath '.\exports\pc' -GameRoot '.\wobble' -Bank Pc

# Effects: extraction preserves original WAVs; installation cleans locally.
.\scripts\Extract-Audio.ps1 -SourcePath 'D:\Games\Wobble' -Destination '.\exports\audio'
.\scripts\Install-Audio.ps1 -SourcePath '.\exports\audio' -GameRoot '.\wobble'
# Pass -Original to retain source effects, including custom/unrecognized banks.

# Export native SD files plus PNGs from TGA/TIM/CMP texture containers.
.\scripts\Extract-Textures.ps1 -SourcePath 'D:\Games\Wobble' -Destination '.\exports\sd'
.\scripts\Install-Textures.ps1 -SourcePath '.\exports\sd' -GameRoot '.\wobble' -Kind Sd

# Install edited HD PNGs. Keep each atlas's canvas and normalized layout.
.\scripts\Install-Textures.ps1 -SourcePath '.\assets\hd' -GameRoot '.\wobble' -Kind Hd
```

HD runtime replacements currently support font-and-numbers.png, hud-gauges.png,
notice.png and title.png. Source PNG dimensions are unrestricted by the native
page size; internal GPU pages are normalized by the renderer. The notice
is fit without distortion; title artwork is fit vertically and cropped at the
sides. Every PNG pixel/alpha byte is retained in the runtime upload.

SD PNG exports are painting references, with native loader row orientation.
Installing SD means encoded native TGA/TIM/CMP/PPM files, not arbitrary PNG
overrides for world textures. CMP export expands its native LZSS/TIM sequence;
individual PNGs are numbered by original order. Native track WAD archives remain
with their geometry. No automatic texture resynthesis or recompression occurs.

For a complete command-line install:

```powershell
.\scripts\Install-Mod.ps1 -GameSource 'D:\Discs\Wipeout.XL.mds' `
    -PsxMusicSource 'D:\Discs\WipEout2097.zip' -DefaultSoundtrack Psx
```

PC game data alone is enough for the minimum install; missing PC music is optional.
Add -PcMusicSource when your game ZIP/data ISO does not contain that audio.
An optional PS1 source can supply music for a data-only PC install; it never replaces PC game data. Use -DefaultSoundtrack None for sound effects only.

Close the game before modifying installed artwork, music or effects. Keep your
original inputs. Maintainer testing/extraction output is excluded from release ZIPs.

Play runs `Refresh-Assets.ps1` before starting the game. To run only this step:

```powershell
.\Play.ps1 -RefreshOnly
# Or use the helper with an explicit installed game folder:
.\scripts\Refresh-Assets.ps1 -GameRoot '.\wobble' -PassThru
```

HD PNG sources are in `assets/hd/`. Setup keeps audio inputs in
`wobble/audio/source/`; these are locally supplied game files, never release
payloads. The audio and texture installers accept `-Incremental` to verify
existing outputs and only regenerate changed/damaged assets. They also detect
changes to the audio processor or artwork packer. `-PassThru` returns update
counts. Setup's HD/original-audio preferences are retained during every refresh.

In a source checkout, double-click `assets/hd/trim.cmd` with FFmpeg on
PATH. It processes PNGs beside the script, verifies decoded pixels/alpha and bit
depth, preserves PNG color-profile chunks, and replaces only smaller outputs.
Editing metadata is removed. Animated PNGs are skipped. Use `--no-pause` from a
terminal to suppress the final keypress prompt.
This maintainer script is kept in Git and excluded from game release ZIPs.

HD artwork lives only in assets/hd. Install-Textures.ps1 validates these PNGs;
the runtime decodes them directly. Play runs Repair-Layout.ps1 on every launch.
Install-Videos.ps1 optionally uses ffmpeg from PATH to create and validate MP4s,
removing MPEG intermediates only after success and temporary outputs on failure.

## Maintainer texture debander

The user-supplied C debanding utility is in `tools/deband/`. Build it with
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/deband/Build.ps1`.
Its Windows executable is `tools/deband/deband.exe`; `-h` shows the available
options. Use `-o` with a separate output directory to retain the source images.
FFmpeg is preferred when installed, with a native C fallback. Build dependencies,
local fixes and fixture checks are documented in
[the local tool instructions](../tools/deband/LOCAL.md).
The executable, dependencies and processed artwork remain ignored, and this
maintainer tool is excluded from game releases.
