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
notice.png and title.png. Font/gauges are square power-of-two pages. The notice
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

The Windows image is enough for the minimum install if it contains its CD audio.
Add -PcMusicSource when your game ZIP/data ISO does not contain that audio.
An optional PS1 source never substitutes for PC game data or PC music.

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
