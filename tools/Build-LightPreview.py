"""Make the pictures of Setup's "Adjust shadows / highlights" preview.

    python tools/Build-LightPreview.py [lit.png full.png bare.png] [--width 640]

The three screenshots are of one held moment of a race, taken with
tools\\capture-light-preview.cmd (F9): the scene lit as the game lights it,
fully lit, and unlit. Without names, the three newest in screenshots\\ are
taken, in that order.

The game's lighting settings change only the light on the scenery's opaque
polygons. A pixel is therefore

    bare + (full - bare) * light

where `bare` is what the settings never touch (sky, effects, display),
`full - bare` is the scenery at full light and `light` is how brightly the
game lights it there, per colour. Setup applies the game's curve to `light`
and puts the picture together again. Written to assets/ui:

    light-preview-bare.png   bare
    light-preview-full.png   full
    light-preview-light.png  the square root of light, which keeps the dark
                             end fine in eight bits
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('pictures', nargs='*', type=Path)
    parser.add_argument('--width', type=int, default=640)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets' / 'ui')
    args = parser.parse_args()
    pictures = args.pictures
    if not pictures:
        pictures = sorted((ROOT / 'screenshots').glob('wo4k-*.png'))[-3:]
    if len(pictures) != 3:
        sys.exit('Three screenshots are needed: lit, fully lit and unlit.')
    lit, full, bare = (Image.open(p).convert('RGB') for p in pictures)
    if not lit.size == full.size == bare.size:
        sys.exit('The three screenshots differ in size.')
    height = round(args.width * lit.size[1] / lit.size[0])
    # Averaged down first: the light is then the average over each pixel's
    # share of the screenshot, weighted by how much scenery is there.
    lit, full, bare = (np.asarray(p.resize((args.width, height), Image.Resampling.BOX), dtype=np.float64)
                       for p in (lit, full, bare))
    scenery = np.maximum(full - bare, 0)
    light = np.ones_like(scenery)
    known = scenery >= 4
    light[known] = np.clip((lit[known] - bare[known]) / scenery[known], 0, 1)
    args.output.mkdir(parents=True, exist_ok=True)
    for name, pixels in (('bare', bare), ('full', np.maximum(full, bare)),
                         ('light', np.sqrt(light) * 255)):
        Image.fromarray(np.clip(pixels + 0.5, 0, 255).astype(np.uint8), 'RGB').save(
            args.output / f'light-preview-{name}.png', optimize=True)
    again = bare + scenery * light
    print('made from', ', '.join(p.name for p in pictures))
    print(f'{args.width}x{height}; put together again it differs from the lit screenshot by '
          f'{np.abs(again - lit).mean():.2f} of 255 on average')


if __name__ == '__main__':
    main()
