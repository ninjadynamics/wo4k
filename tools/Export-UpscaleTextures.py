"""Export the installed, approved native selection without changing game files.

The manifest maps flat, editable PNGs back to all native sheet rectangles.
Original PNGs and sheets are retained in baseline.zip beside the manifest.
Requires the maintainer dependencies Pillow and numpy, not shipped with Play.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_sheet(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<HH', data, 12)
    if data[2] != 2 or data[16] != 16 or len(data) < 18 + data[0] + width * height * 2:
        raise ValueError(f'Expected native uncompressed RGB555 sheet: {path}')
    words = np.frombuffer(data, dtype='<u2', count=width * height,
                          offset=18 + data[0]).reshape(height, width)
    # Match the native loader, which ignores TGA origin flags. Bit 15 is
    # retained in the baseline; it is not general-purpose 8-bit alpha.
    rgb = np.stack([(words >> shift & 31) * 255 // 31
                    for shift in (10, 5, 0)], axis=2).astype('uint8')
    return data, rgb


def free_rectangles(mask):
    """Partition uncovered space without crossing any selected rectangle.

    Adjacent equal horizontal runs merge vertically. Atlas boundaries are
    handled by the caller, so no export straddles a native 256-pixel page.
    """
    active = {}
    for y in range(mask.shape[0] + 1):
        runs = []
        if y < mask.shape[0]:
            edges = np.diff(np.pad((~mask[y]).astype('int8'), (1, 1)))
            runs = list(zip(np.flatnonzero(edges == 1), np.flatnonzero(edges == -1)))
        current = set(runs)
        for run in list(active):
            if run not in current:
                top = active.pop(run)
                yield int(run[0]), top, int(run[1] - run[0]), y - top
        for run in runs:
            active.setdefault(run, y)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, default=Path('wobble'))
    parser.add_argument('--output', type=Path, default=Path('exports/upscale-textures'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    game = args.game_root.resolve()
    out = args.output.resolve()
    # Never overwrite a user's upscaled work on a second invocation.
    if out.exists():
        raise FileExistsError(f'Choose a new output directory; preserving existing work: {out}')
    plan_path = root / 'manifests/texture-selection.json'
    plan = json.loads(plan_path.read_text())
    receipt_path = game / 'textures/selection.json'
    receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    if receipt.get('revision') != plan['revision'] or receipt.get('pending_ps1_regions') != 0 or not receipt.get('ps1_textures'):
        raise ValueError('Install the complete approved PC/PS1 texture selection before exporting')
    sd = game / 'textures/sd'
    paths = {p.relative_to(sd).as_posix().upper(): p
             for p in (sd / 'Pics').rglob('*') if p.suffix.lower() == '.tga'}
    for record in receipt['files']:
        if sha(paths[record['file'].upper()].read_bytes()) != record['sha256']:
            raise ValueError(f'Installed selection differs from its receipt: {record["file"]}')
    hd_plan = json.loads((root / 'manifests/hd-textures.json').read_text())
    hd_coverage = {}
    protected = []
    for record in hd_plan:
        path = root / 'assets/auth' / record['file']
        if not path.is_file():
            continue
        protected.append(dict(file=path.relative_to(root).as_posix(), sha256=sha(path.read_bytes())))
        # Banner-only overlays do not cover a complete native page.
        if record['kind'] == 'atlas':
            hd_coverage[(record['native'].upper(), record['page'])] = record['file']

    sheets, excluded = {}, []
    for name, path in sorted(paths.items()):
        # Lower resolution inputs are alternate consumers, not duplicate art
        # jobs. Require exact decoded equality before sharing an editable file.
        if '/HI-RES/' not in name:
            continue
        data, rgb = read_sheet(path)
        mirrors = [name]
        low = name.replace('/HI-RES/', '/LO-RES/')
        if low in paths:
            _, lo = read_sheet(paths[low])
            if np.array_equal(lo, rgb):
                mirrors.append(low)
            else:
                sheets[low] = dict(data=paths[low].read_bytes(), rgb=lo, mirrors=[low])
        sheets[name] = dict(data=data, rgb=rgb, mirrors=mirrors)

    for name, sheet in sheets.items():
        h, w = sheet['rgb'].shape[:2]
        sheet['mask'] = np.zeros((h, w), dtype=bool)
        sheet['coverage'] = np.zeros((h, w), dtype=bool)
        for page in range((w + 255) // 256):
            hd = hd_coverage.get((Path(name).name, page))
            if hd:
                sheet['mask'][:, page*256:(page+1)*256] = True
                excluded.append(dict(atlas=name, page=page, existing_hd='assets/auth/' + hd))

    groups = collections.OrderedDict()
    for op in plan['operations']:
        name = op['atlas'].upper()
        if name not in sheets:
            continue
        if any(name in s['mirrors'][1:] for s in sheets.values()):
            continue
        x, y, w, h = (op[k] for k in ('x', 'y', 'width', 'height'))
        if sheets[name]['mask'][y:y+h, x:x+w].any():
            raise ValueError('Selected texture partially intersects an existing HD page')
        groups.setdefault(op['label'], []).append(op)

    (out / 'textures').mkdir(parents=True)
    entries = []
    sources = []
    def place(name, rect):
        x, y, w, h = rect
        return [dict(atlas=mirror, x=x, y=y, width=w, height=h,
                     page=x//256, page_x=x%256, page_y=y)
                for mirror in sheets[name]['mirrors']]

    with zipfile.ZipFile(out / 'baseline.zip', 'w', zipfile.ZIP_DEFLATED) as baseline:
        baseline.writestr('texture-selection.json', plan_path.read_bytes())
        baseline.writestr('selection-receipt.json', receipt_path.read_bytes())
        for name, sheet in sheets.items():
            for mirror in sheet['mirrors']:
                raw = paths[mirror].read_bytes()
                baseline.writestr('native/' + mirror, raw)
                sources.append(dict(atlas=mirror, sha256=sha(raw),
                                    width=sheet['rgb'].shape[1], height=sheet['rgb'].shape[0]))

        def save(identity, pixels, kind, provenance, placements, source=None):
            filename = re.sub(r'[^a-zA-Z0-9_-]+', '-', identity).strip('-').lower() + '.png'
            path = out / 'textures' / filename
            if path.exists():
                raise ValueError(f'Filename collision: {filename}')
            # The game keys exact black on every sheet, and the installer
            # stores the PlayStation's opaque black as near-black, so exact
            # black is precisely what is see-through. Say so in the PNG: an
            # HD replacement with transparency is drawn by its own alpha.
            keyed = ~pixels.any(axis=2)
            image = Image.fromarray(pixels)
            if keyed.any():
                image.putalpha(Image.fromarray(np.where(keyed, 0, 255).astype('uint8')))
            image.save(path)
            raw = path.read_bytes()
            baseline.writestr('textures/' + filename, raw)
            entries.append(dict(id=identity, file='textures/'+filename,
                                kind=kind, source=source, provenance=provenance,
                                width=image.width, height=image.height, mode=image.mode,
                                png_sha256=sha(raw), rgb_sha256=sha(pixels.tobytes()),
                                placements=placements))

        for label, operations in groups.items():
            op = operations[0]
            source = op['source']
            road = 'material' in source
            identity = label
            if road:
                provenance = 'PC tile library verified byte-identical to PS1; reconstructed PS1 road artwork'
                kind = 'road' if source['level'] == 128 else 'authored-road-mip'
            else:
                provenance = 'PS1 selected; native source bytes differ' if 'ps1' in source else 'PC source verified byte-identical to PS1'
                kind = 'texture'
            pixels = None
            placements = []
            for target in operations:
                name = target['atlas'].upper()
                x, y, w, h = (target[k] for k in ('x', 'y', 'width', 'height'))
                crop = sheets[name]['rgb'][y:y+h, x:x+w]
                if pixels is not None and not np.array_equal(pixels, crop):
                    raise ValueError(f'Selected placements disagree: {label}')
                pixels = crop
                sheets[name]['mask'][y:y+h, x:x+w] = True
                sheets[name]['coverage'][y:y+h, x:x+w] = True
                placements.extend(place(name, (x, y, w, h)))
            save(identity, pixels, kind, provenance, placements, source)

        for name, sheet in sheets.items():
            h, w = sheet['rgb'].shape[:2]
            for page in range((w+255)//256):
                left, right = page*256, min(w, (page+1)*256)
                for index, (x, y, rw, rh) in enumerate(free_rectangles(sheet['mask'][:, left:right])):
                    x += left
                    pixels = sheet['rgb'][y:y+rh, x:x+rw]
                    # Empty black padding/fill has no detail to upscale. The
                    # exact native bytes remain in baseline.zip for repacking.
                    if not pixels.any():
                        continue
                    identity = f'pc-{Path(name).stem}-page{page:02d}-region{index:03d}'
                    if '/LO-RES/' in name:
                        identity += '-lo-res'
                    save(identity, pixels, 'native-atlas-section',
                         'Remaining native PC artwork outside the fixed selection map; preserve layout within this section',
                         place(name, (x, y, rw, rh)), dict(native_atlas=name))
                    sheet['coverage'][y:y+rh, x:x+rw] = True

        # Reconstruct every pixel from exported images, separately tracking
        # holes and overlap, to establish round-trip coverage before handoff.
        reconstructed = {name: np.zeros_like(sheet['rgb']) for name, sheet in sheets.items()}
        for entry in entries:
            with Image.open(out / entry['file']) as image:
                pixels = np.array(image)[:, :, :3]
            for dest in entry['placements']:
                if dest['atlas'] not in reconstructed:
                    continue
                x, y, w, h = (dest[k] for k in ('x', 'y', 'width', 'height'))
                reconstructed[dest['atlas']][y:y+h, x:x+w] = pixels
        for name, sheet in sheets.items():
            check = sheet['coverage'] | ~sheet['mask']
            if not np.array_equal(reconstructed[name][check], sheet['rgb'][check]):
                raise ValueError(f'Export round trip failed: {name}')

    counts = dict(collections.Counter(e['kind'] for e in entries))
    manifest = dict(format='wo4k.upscale-export', version=1,
                    selection_revision=plan['revision'], selection_sha256=sha(plan_path.read_bytes()),
                    texture_folder='textures', baseline_archive='baseline.zip',
                    baseline_sha256=sha((out/'baseline.zip').read_bytes()),
                    counts=counts, total_images=len(entries),
                    logical_comparison_textures=sum(e['kind'] in ('road', 'texture') for e in entries),
                    existing_hd_excluded=excluded, protected_hd_files=protected,
                    native_sheets=sources,
                    pixel_conventions=dict(orientation='native loader row order; no vertical flip',
                        color='native RGB555 expanded by channel*255//31; no colour adjustment',
                        alpha='Exact native black, which the game keys on every sheet, is exported fully transparent (RGBA); the opaque black of the PlayStation data is near-black and stays opaque. Fully opaque textures are RGB. Additive/subtractive blending is per draw.',
                        original_bit15='Preserved in baseline native sheets; not converted to PNG alpha'),
                    reintegration=dict(status='export only; not installed',
                        coordinates='Unscaled native sheet pixels; page_x/page_y address the 256x256 page',
                        policy='Rebuild square HD pages from the baseline and listed placements, scaling coordinates consistently. Preserve existing HD aliases and native draw blend modes. Do not quantize enhanced art back to RGB555.',
                        mips='Authored road mip levels exported separately for fidelity; their identities and levels are recorded in source.',
                        unchanged_regions='Existing HD pages are excluded. Empty black native regions remain baseline fill.',
                        dimensions='Each PNG may be enlarged; preserve its aspect ratio, filename, full canvas, and orientation. Derive scale from new dimensions, never manifest original dimensions alone.'),
                    validation=dict(selection_receipt_hashes_verified=True,
                        decoded_hi_lo_mirrors_verified=True, all_exported_pngs_reopened=True,
                        all_unexcluded_native_rgb_pixels_round_trip=True), textures=entries)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    (out/'README.txt').write_text(
        'WO4K native texture upscale workspace\n\n'
        f'{len(entries)} editable PNGs are together in textures/.\n'
        'These are the installed, approved PC/PS1 choices at original resolution.\n'
        'Pages already replaced by existing HD artwork are omitted.\n'
        '484 mapped base textures are accompanied by authored road mip levels\n'
        'and remaining PC atlas sections, preserving every uncovered native pixel.\n\n'
        'Overwrite the PNGs with your upscaled results. Keep the filenames, aspect\n'
        'ratios, full canvases and orientation. Do not trim, rotate, or add borders.\n'
        'Atlas sections can contain several neighboring cells: preserve their layout.\n'
        'See-through areas are transparent in the PNGs; black that is still opaque is\n'
        'solid paint. Keep the alpha channel through upscaling: a PNG with transparency\n'
        'is drawn by its own alpha, one without it by the original cut-out. Do not\n'
        'paint over the black of glow and flare textures, which the game adds on screen.\n'
        'Use a consistent integer enlargement factor where possible.\n\n'
        'manifest.json records native rectangles, every shared destination, sources,\n'
        'mip levels, hashes, protected HD artwork and reconstruction instructions.\n'
        'baseline.zip preserves all original exported PNGs and original native\n'
        'sheets. Leave it and the manifest untouched when overwriting PNGs.\n'
        'No game files or assets/auth files were changed. Individual PNGs will need\n'
        'packing into HD pages after you approve the results.\n', encoding='utf-8')
    print(json.dumps(dict(folder=str(out/'textures'), images=len(entries), counts=counts,
                          excluded_hd_pages=len(excluded), verified=True)))


if __name__ == '__main__':
    main()
