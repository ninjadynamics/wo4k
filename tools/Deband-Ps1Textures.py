"""Export every PlayStation texture frame as a PNG and deband the set.

Maintainer tool; changes no game files. Reads a PlayStation disc (a ZIP holding
the data track, or the .bin itself), writes one PNG per TIM frame to --output
(see-through texels transparent, the opaque black of the disc data kept), then
runs tools/deband/deband.exe twice into --debanded (v1: smoother, details
ramped too; v2: details kept) and builds the comparison page from
tools/texture-picker in --page, where each texture keeps the original, v1
or v2 and the choices export as JSON.

Scenery frames that meet edge to edge in the game are debanded together: the
track models say which frame borders touch, each frame is processed with a
margin copied from its neighbours, and the margin is cut off again. A gradient
that runs across two frames then gets the same ramp on both sides of the seam.

Requires Pillow and numpy.
"""
import argparse
import collections
import json
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from texture_formats import u16, u32, unpack_cmp  # noqa: E402

MARGIN = 16  # texels of neighbour context; more than deband's band reach
# deband options per level: v1 is close to plain band ramps, v2 protects details
LEVELS = {'v1': ['-k', '0.25'], 'v2': ['-k', '1']}
TRACKS = {'TRACK01': "Talon’s Reach", 'TRACK08': 'Sagarmatha', 'TRACK13': 'Valparaiso',
          'TRACK20': 'Phenitia Park', 'TRACK02': 'Gare d’Europa', 'TRACK17': 'Odessa Keys',
          'TRACK06': 'Vostok Island', 'TRACK07': 'Spilskinanke'}


# --- the disc ----------------------------------------------------------------------
def disc_files(path):
    """{path below WIPEOUT2/: bytes} for the texture archives and track WADs."""
    if path.suffix.lower() == '.zip':
        archive = zipfile.ZipFile(path)
        names = sorted(n for n in archive.namelist() if n.lower().endswith(('.bin', '.img', '.iso')))
        image = archive.read(names[0])
    else:
        image = path.read_bytes()
    raw = len(image) % 2352 == 0 and image[:12] == b'\x00' + b'\xff' * 10 + b'\x00'

    def sector(n):
        return image[n * 2352 + 24:n * 2352 + 2072] if raw else image[n * 2048:(n + 1) * 2048]

    def read(lba, size):
        return b''.join(sector(lba + i) for i in range((size + 2047) // 2048))[:size]

    def walk(lba, size, prefix=''):
        data, at = read(lba, size), 0
        while at < len(data):
            length = data[at]
            if length == 0:
                at = (at // 2048 + 1) * 2048
                continue
            where, count = struct.unpack_from('<I', data, at + 2)[0], struct.unpack_from('<I', data, at + 10)[0]
            name = data[at + 33:at + 33 + data[at + 32]].decode('latin1').split(';')[0]
            if name not in ('\x00', '\x01'):
                if data[at + 25] & 2:
                    yield from walk(where, count, prefix + name + '/')
                else:
                    yield prefix + name, where, count
            at += length

    root = sector(16)[156:190]
    files = {}
    for name, where, count in walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]):
        if name.upper().startswith('WIPEOUT2/') and name.upper().endswith(('.CMP', '.TIM', '.WAD')):
            files[name[9:].upper()] = read(where, count)
    return files


def wad_entries(data):
    count, at, entries = struct.unpack_from('<H', data)[0], 2, []
    for _ in range(count):
        entries.append((data[at:at + 16].split(b'\0')[0].decode(), struct.unpack_from('<I', data, at + 16)[0]))
        at += 25
    out = {}
    for name, size in entries:
        out[name] = data[at:at + size]
        at += size
    return out


def tim_image(tim):
    """RGBA: colour 0 transparent, everything else (opaque black included) solid."""
    if len(tim) < 20 or u32(tim, 0) != 16:
        return None
    flags, at, palette = u32(tim, 4), 8, None
    mode = flags & 7
    if mode > 2:
        return None
    if flags & 8:
        palette = [u16(tim, at + 12 + k * 2) for k in range(16 if mode == 0 else 256)]
        at += u32(tim, at)
    words, height = u16(tim, at + 8), u16(tim, at + 10)
    width = words * (4, 2, 1)[mode]
    pixels = bytearray()
    for y in range(height):
        row = at + 12 + y * words * 2
        for x in range(width):
            if mode == 0:
                c = palette[(tim[row + x // 2] >> ((x & 1) * 4)) & 15]
            elif mode == 1:
                c = palette[tim[row + x]]
            else:
                c = u16(tim, row + x * 2)
            pixels += bytes(((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31,
                             0 if c == 0 else 255))
    return Image.frombytes('RGBA', (width, height), bytes(pixels))


# --- which frame borders meet --------------------------------------------------------
def scene_prims(data):
    """Textured primitives of a scene.prm: (texture, world vertices, uvs)."""
    at = 0

    def i16():
        nonlocal at
        value = struct.unpack_from('>h', data, at)[0]
        at += 2
        return value

    def i32():
        nonlocal at
        value = struct.unpack_from('>i', data, at)[0]
        at += 4
        return value

    # per primitive type: vertex indices, extra index words, textured, bytes after the uvs
    layout = {1: (3, 0, 0, 6), 2: (3, 0, 1, 6), 3: (4, 0, 0, 4), 4: (4, 0, 1, 6), 5: (3, 0, 0, 14),
              6: (3, 0, 1, 14), 7: (4, 0, 0, 16), 8: (4, 0, 1, 18), 9: (3, 1, 0, 4), 10: (3, 1, 1, 4),
              12: (4, 1, 1, 4), 13: (3, 3, 0, 12), 14: (3, 3, 1, 12), 15: (4, 4, 0, 16), 16: (4, 4, 1, 18),
              17: (1, 3, 0, 4), 18: (1, 3, 0, 4)}
    skipped = {11: 12, 20: 52, 21: 24, 22: 36, 23: 12}
    while at < len(data):
        at += 16
        vertices = i16()
        at += 6
        normals = i16()
        at += 6
        primitives = i16()
        at += 6 + 12 + 4 + 2 + 2 + 4 + 18 + 2
        origin = (i32(), i32(), i32())
        at += 18 + 2 + 12 + 2 + 2 + 12
        points = []
        for _ in range(vertices):
            points.append((i16(), i16(), i16()))
            at += 2
        at += normals * 8
        for _ in range(primitives):
            kind = i16()
            at += 2
            if kind in skipped:
                at += skipped[kind]
                continue
            corners, extra, textured, tail = layout[kind]
            index = [i16() for _ in range(corners)]
            at += extra * 2
            if textured:
                texture = i16()
                at += 4
                uvs = [(data[at + k * 2], data[at + k * 2 + 1]) for k in range(corners)]
                at += corners * 2
                if all(i < len(points) for i in index):
                    yield texture, [tuple(origin[k] + points[i][k] for k in range(3)) for i in index], uvs
            at += tail


def border(a, b, size):
    """The frame border an edge lies along, with the along-border coordinates of its ends."""
    (u1, v1), (u2, v2), (width, height) = a, b, size
    if abs(u1 - u2) <= 1 and abs(v1 - v2) >= 0.6 * height:
        if u1 <= 2:
            return 'L', v1, v2
        if u1 >= width - 3:
            return 'R', v1, v2
    if abs(v1 - v2) <= 1 and abs(u1 - u2) >= 0.6 * width:
        if v1 <= 2:
            return 'T', u1, u2
        if v1 >= height - 3:
            return 'B', u1, u2
    return None


def frame_joins(prm, sizes):
    """{(frame, side): (other frame, its side, reversed)} for borders that meet in the model."""
    edges = collections.defaultdict(list)
    for texture, corners, uvs in scene_prims(prm):
        order = [(0, 1), (1, 3), (3, 2), (2, 0)] if len(corners) == 4 else [(0, 1), (1, 2), (2, 0)]
        for i, j in order:
            first = corners[i] < corners[j]
            key = (corners[i], corners[j]) if first else (corners[j], corners[i])
            edges[key].append((texture, uvs[i] if first else uvs[j], uvs[j] if first else uvs[i]))
    votes = collections.Counter()
    for shared in edges.values():
        for ta, a1, a2 in shared:
            for tb, b1, b2 in shared:
                if ta == tb or max(ta, tb) >= len(sizes) or not sizes[ta] or not sizes[tb]:
                    continue
                side_a, side_b = border(a1, a2, sizes[ta]), border(b1, b2, sizes[tb])
                if side_a and side_b:
                    votes[(ta, side_a[0], tb, side_b[0], (side_a[2] > side_a[1]) != (side_b[2] > side_b[1]))] += 1
    joins = {}
    for (ta, side_a, tb, side_b, reverse), count in votes.most_common():
        if count >= 2:  # a single coincident edge is usually two unrelated faces
            joins.setdefault((ta, side_a), (tb, side_b, reverse))
    return joins


def with_margins(frames, index, joins):
    """Frame `index` on a canvas with MARGIN texels of each neighbour that meets it."""
    own = np.array(frames[index])
    height, width = own.shape[:2]
    canvas = np.zeros((height + 2 * MARGIN, width + 2 * MARGIN, 4), 'uint8')
    canvas[MARGIN:MARGIN + height, MARGIN:MARGIN + width] = own
    for side in 'LRTB':
        join = joins.get((index, side))
        if not join or frames[join[0]] is None:
            continue
        other = np.array(frames[join[0]])
        # strip[depth][along]: depth 0 is the neighbour's own border line
        strip = {'L': other.transpose(1, 0, 2), 'R': other.transpose(1, 0, 2)[::-1],
                 'T': other, 'B': other[::-1]}[join[1]][:MARGIN]
        if join[2]:
            strip = strip[:, ::-1]
        length = height if side in 'LR' else width
        strip = strip[:, np.arange(length) * strip.shape[1] // length]
        for depth in range(strip.shape[0]):
            if side == 'L':
                canvas[MARGIN:MARGIN + height, MARGIN - 1 - depth] = strip[depth]
            elif side == 'R':
                canvas[MARGIN:MARGIN + height, MARGIN + width + depth] = strip[depth]
            elif side == 'T':
                canvas[MARGIN - 1 - depth, MARGIN:MARGIN + width] = strip[depth]
            else:
                canvas[MARGIN + height + depth, MARGIN:MARGIN + width] = strip[depth]
    return Image.fromarray(canvas)


# --- the comparison page -------------------------------------------------------------
def build_page(root, items, output, debanded, page):
    page.mkdir(parents=True, exist_ok=True)
    order = {'Scenery': 0, 'Sky': 1, 'Shared textures': 2, 'Effects & sprites': 3, 'Road tiles': 4, 'Track other': 5}
    entries = []
    for item in items:
        top, stem = item['source'].split('/')[0], Path(item['source']).stem.upper()
        if top.startswith('TRACK'):
            group = {'SCENE': 'Scenery', 'SKY': 'Sky', 'LIBRARY': 'Road tiles'}.get(stem, 'Track other')
            title = TRACKS.get(top, top)
        else:
            group = 'Effects & sprites' if top == 'TEXTURES' else 'Shared textures'
            title = Path(item['source']).stem.replace('_', ' ').title()
        before = Image.open(output / item['file'])
        entry = {'id': f"{item['source']}#{item['frame']:04d}", 'file': item['file'], 'source': item['source'],
                 'frame': item['frame'], 'group': group, 'title': title, 'width': item['width'],
                 'height': item['height'], 'defaultVersion': 'v1', 'identical': {},
                 'original': Path('..', '..', *output.relative_to(root).parts, item['file']).as_posix()}
        for level in LEVELS:
            after = Image.open(debanded / level / item['file'])
            entry['identical'][level] = before.mode == after.mode and before.tobytes() == after.tobytes()
            entry[level] = Path('..', '..', *debanded.relative_to(root).parts, level, item['file']).as_posix()
        entries.append(entry)
    entries.sort(key=lambda e: (order[e['group']], e['source'], e['frame']))
    catalog = {'format': 'wo4k.texture-catalog', 'version': 1, 'catalogRevision': 'deband-2', 'textures': entries}
    data = json.dumps(catalog, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')

    text = (root / 'tools/texture-picker/index.html').read_text(encoding='utf-8')
    for old, new in [
            ('WO4K · Texture choices', 'WO4K · Deband comparison'), ('PC × PLAYSTATION', 'PLAYSTATION × DEBAND'),
            ('<strong>PS1 on the left, PC on the right.</strong> Choose the version you want to keep',
             '<strong>Original PS1 on the left, debanded on the right.</strong> Keep the original, deband v1 '
             '(smoother) or deband v2 (keeps details) for each texture'),
            ('Road comparisons include the richer PS1 colours recovered from PC data. Other previews show original '
             'texture pixels; in-game blending is not simulated.',
             'Every texture frame on the PlayStation disc. The right side shows the version you keep; with the '
             'original kept it shows the debanded version last chosen. In-game blending is not simulated.'),
            ('<b class="ps1-dot"></b>PS1 <b class="pc-dot"></b>PC',
             '<b class="ps1-dot"></b>Original <b class="pc-dot"></b>Debanded'),
            ('<!-- CATALOG -->', '<script type="application/json" id="catalog">' + data + '</script>'),
            ('<script src="app.js"></script>', '<script src="deband.js"></script>')]:
        assert old in text, old
        text = text.replace(old, new)
    (page / 'index.html').write_text(text, encoding='utf-8')
    for name in ('deband.js', 'style.css'):
        shutil.copyfile(root / 'tools/texture-picker' / name, page / name)
    stale = page / 'app.js'
    if stale.exists():
        stale.unlink()


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--disc', type=Path, required=True, help='PlayStation disc: ZIP or data-track .bin')
    parser.add_argument('--output', type=Path, default=root / 'assets/sd-original')
    parser.add_argument('--debanded', type=Path, default=root / 'assets/sd-debanded')
    parser.add_argument('--page', type=Path, default=root / 'exports/ps1-deband-comparison')
    parser.add_argument('--deband', nargs=argparse.REMAINDER, default=[],
                        help='further options passed on to deband.exe for both levels')
    args = parser.parse_args()
    output, debanded = args.output.resolve(), args.debanded.resolve()
    for folder in (output, debanded):
        if folder.exists() and any(folder.iterdir()):
            parser.error(f'{folder} is not empty; move it away first')
    work = root / '.work/deband-ps1'
    shutil.rmtree(work, ignore_errors=True)
    plain, joined = work / 'plain', work / 'joined'
    for folder in (output, plain, joined):
        folder.mkdir(parents=True, exist_ok=True)

    files = disc_files(args.disc)
    items, margins = [], 0
    for name in sorted(files):
        if name.endswith('.WAD'):
            continue
        try:
            raw_frames = unpack_cmp(files[name]) if name.endswith('.CMP') else [files[name]]
        except AssertionError:
            continue
        frames = [tim_image(f) if f else None for f in raw_frames]
        joins = {}
        track = name.split('/')[0]
        if name.endswith('/SCENE.CMP') and track + '/TRACK.WAD' in files:
            prm = wad_entries(files[track + '/TRACK.WAD']).get('scene.prm')
            if prm:
                joins = frame_joins(prm, [f.size if f else None for f in frames])
        stem = name.lower().replace('/', '-').replace('.', '-').replace('~', '_')
        for index, frame in enumerate(frames):
            if frame is None:
                continue
            file = f'{stem}-{index}.png'
            frame.save(output / file)
            meets = sorted(side for (i, side) in joins if i == index)
            if meets:
                with_margins(frames, index, joins).save(joined / file)
                margins += 1
            else:
                frame.save(plain / file)
            items.append({'file': file, 'source': name, 'frame': index, 'width': frame.width, 'height': frame.height,
                          'joined': {side: {'frame': joins[(index, side)][0], 'side': joins[(index, side)][1],
                                            'reversed': joins[(index, side)][2]} for side in meets}})
    (output / 'index.json').write_text(json.dumps(items, indent=1), encoding='utf-8')

    exe = root / 'tools/deband/deband.exe'
    for level, options in LEVELS.items():
        for source in (plain, joined):
            if any(source.iterdir()):
                subprocess.run([str(exe), *options, *args.deband, '-o', str(work / f'{source.name}-{level}'),
                                str(source)], check=True)
        (debanded / level).mkdir(parents=True, exist_ok=True)
        for item in items:
            target = debanded / level / item['file']
            if item['joined']:
                image = Image.open(work / f'joined-{level}' / item['file'])
                image.crop((MARGIN, MARGIN, MARGIN + item['width'], MARGIN + item['height'])).save(target)
            else:
                shutil.copyfile(work / f'plain-{level}' / item['file'], target)
    shutil.rmtree(work, ignore_errors=True)
    build_page(root, items, output, debanded, args.page.resolve())
    print(json.dumps({'frames': len(items), 'debanded_with_neighbours': margins, 'page': str(args.page / 'index.html')}))


if __name__ == '__main__':
    main()
