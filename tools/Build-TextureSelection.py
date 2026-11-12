"""Project-time PC/PS1 byte audit and fixed native-atlas placement map.

Requires Pillow and numpy for the maintainer audit only. Setup uses the checked-in
map and its C# decoder; neither Python nor asset-comparison code runs in the mod.
"""
import argparse
import collections
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
from PIL import Image
from texture_formats import unpack_cmp, decode_tim


def sha(b):
    return hashlib.sha256(b).hexdigest()


def wad_entries(path):
    data = path.read_bytes()
    count = struct.unpack_from('<H', data)[0]
    assert 0 < count <= 256
    offset, entries = 2, []
    for _ in range(count):
        end = data.index(b'\0', offset, offset + 64)
        name = data[offset:end].decode('ascii').lower()
        offset = end + 1
        for candidate in range(offset + 4, offset + 13):
            size, copy = struct.unpack_from('<II', data, candidate)
            if 0 < size <= len(data) and size == copy and data[candidate + 8] == 0:
                entries.append((name, size))
                offset = candidate + 9
                break
        else:
            raise ValueError('Malformed WAD directory')
    assert sum(size for _, size in entries) == len(data) - offset
    result = {}
    for name, size in entries:
        result[name] = data[offset:offset + size]
        offset += size
    return result


def tga(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<HH', data, 12)
    assert data[2] == 2 and data[16] == 16
    pixels = np.frombuffer(data, dtype='<u2', count=width * height, offset=18 + data[0]).reshape(height, width)
    # The PC loader reads raw row order, ignoring the origin flag.
    rgb = np.stack([(pixels >> shift & 31) * 255 // 31 for shift in [10, 5, 0]], axis=2).astype('uint8')
    return Image.fromarray(rgb)


def locate(needle, haystack):
    """All exact rectangular occurrences; no similarity/colour-count selection."""
    w, h = needle.size
    big_w, big_h = haystack.size
    if w > 256 or h > big_h or w < 8 or h < 8:
        return []
    raw, big = needle.tobytes(), haystack.tobytes()
    # Prefer a distinctive row to avoid scanning every background-black pixel.
    row = max(range(h), key=lambda y: len(set(raw[y * w * 3:(y + 1) * w * 3])))
    probe = raw[row * w * 3:(row + 1) * w * 3]
    if len(set(probe)) < 3:
        return []
    found, start = [], 0
    while True:
        at = big.find(probe, start)
        if at < 0:
            break
        start = at + 1
        if at % 3:
            continue
        x, y = at // 3 % big_w, at // 3 // big_w - row
        # Native pages never allow a texture rectangle to cross a page boundary.
        if y < 0 or y + h > big_h or x % 256 + w > 256:
            continue
        base = (y * big_w + x) * 3
        if all(big[base + k * big_w * 3:base + k * big_w * 3 + w * 3] == raw[k * w * 3:(k + 1) * w * 3] for k in range(h)):
            found.append((x, y))
    return found


# The PC release's TRACK17 scene.cmp places the grass tile at VRAM x 911,
# four texels over the 64-wide wall tile before it; the PlayStation release
# has 912. The plan corrects that byte (a literal in the compressed stream)
# and lays both tiles out as on the PlayStation, where the game then looks.
GRASS_FIX = {'id': 'odessa-keys-grass', 'file': 'TRACK17/TRACK.WAD', 'offset': 286485, 'from': 0x8f, 'to': 0x90,
             'sha256': 'a7581fe470abac7f65751ec2c70db3affb579585037c28f98748fb7e080ca886',
             'patched_sha256': '9809dcacf7649fbb9580d47f4da08cf4deb174517c2ae2d878a115ded30dcd56'}


def layout_corrections(operations):
    def moved(op):
        return Path(op['atlas']).stem.upper() == 'NRACE06' and op['y'] == 0 and 2304 <= op['x'] < 2432
    kept = [op for op in operations if not moved(op)]
    for atlas in dict.fromkeys(op['atlas'] for op in operations if Path(op['atlas']).stem.upper() == 'NRACE06'):
        for frame, x in ((25, 2304), (26, 2368)):
            kept.append(dict(atlas=atlas, x=x, y=0, width=64, height=64,
                             source={'wad': GRASS_FIX['file'], 'entry': 'scene.cmp', 'frame': frame},
                             label=f'TRACK17/PC-SCENE.CMP/{frame}', patch=GRASS_FIX['id']))
    return kept, [GRASS_FIX]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pc-root', type=Path, required=True)
    parser.add_argument('--ps1-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--audit', type=Path)
    args = parser.parse_args()
    sd = args.pc_root / 'textures/sd'
    pc_files = {p.relative_to(sd).as_posix().upper(): p.read_bytes() for p in sd.rglob('*') if p.is_file() and p.suffix.lower() in ['.cmp', '.tim'] and 'exports' not in p.parts}
    pc_sources = {name: {'path': name} for name in pc_files}
    wads = {}
    for path in sorted(args.pc_root.glob('TRACK*/TRACK.WAD')):
        entries = wad_entries(path)
        wads[path.parent.name.upper()] = entries
        for name, data in entries.items():
            if Path(name).suffix in ['.cmp', '.tim']:
                key = path.parent.name.upper() + '/' + name.upper()
                pc_files[key] = data
                pc_sources[key] = {'wad': path.relative_to(args.pc_root).as_posix(), 'entry': name}
    native = {}
    native_sha = {}  # the sheets the placements are located in, pinned in the plan
    for path in sorted((sd / 'Pics').rglob('*')):
        if path.suffix.lower() != '.tga':
            continue
        # Menu geometry/HD art stays authored for the PC interface. Include menu
        # ship preview atlases, shadows and every race/sky atlas at both settings.
        if not (path.stem.upper().startswith(('NRACE', 'SKY')) or path.stem.upper() in ['MENUHF', 'SILLYSHP', 'SILLSHAD']):
            continue
        name = path.relative_to(sd).as_posix()
        pristine = args.pc_root / 'textures/native-source' / name
        native[name] = tga(pristine if pristine.is_file() else path)
        native_sha[name] = sha((pristine if pristine.is_file() else path).read_bytes())
    pc_tim = {}
    pc_tim_bytes = {}
    pc_rgb = collections.defaultdict(list)
    for name, data in pc_files.items():
        frames = unpack_cmp(data) if name.endswith('.CMP') else [data]
        for index, frame in enumerate(frames):
            if not frame:
                continue
            image = decode_tim(frame)[0]
            source = dict(pc_sources[name], frame=index)
            pc_tim.setdefault(sha(frame), source)
            pc_tim_bytes.setdefault(sha(frame), frame)
            pc_rgb[(image.size, sha(image.tobytes()))].append((name, index, image, source))
    selections, operations, audit, remaining = [], [], [], []
    seen = set()
    cache = {}
    def positions(image):
        key = (image.size, sha(image.tobytes()))
        if key not in cache:
            cache[key] = [(name, x, y) for name, atlas in native.items() for x, y in locate(image, atlas)]
        return cache[key]
    def add(image, source, tag, targets=None):
        count = 0
        for name, x, y in (positions(image) if targets is None else targets):
            key = (name, x, y, image.size)
            if key in seen:
                continue
            seen.add(key)
            operations.append(dict(atlas=name, x=x, y=y, width=image.width, height=image.height, source=source, label=tag))
            count += 1
        return count

    # Recover full-colour roads and their authored 64/32 mip tiles from the PC
    # WAD's byte-identical PS1 originals. Layout coordinates are audited now.
    # Verified raceTracks[] order in the supported PC EXE. Directory numbers
    # are not the race/NRACE sheet numbers.
    race_order = ['TRACK01', 'TRACK08', 'TRACK13', 'TRACK20', 'TRACK02', 'TRACK17', 'TRACK06', 'TRACK07']
    for track, entries in sorted(wads.items()):
        number = race_order.index(track) + 1
        assert entries['library.cmp'] == (args.ps1_root / track / 'LIBRARY.CMP').read_bytes()
        assert entries['library.ttf'] == (args.ps1_root / track / 'LIBRARY.TTF').read_bytes()
        tiles = [decode_tim(f)[0] for f in unpack_cmp(entries['library.cmp'])]
        pc_road = [decode_tim(f)[0] for f in unpack_cmp(pc_files[track + '/TRACKTEX.CMP'])]
        table = entries['library.ttf']
        for index, old in enumerate(pc_road):
            ref = struct.unpack_from('>21H', table, index * 42)
            for level, count, offset in [(128, 16, 0), (64, 4, 16), (32, 1, 20)]:
                side = level // 32
                image = Image.new('RGB', (level, level))
                for j in range(count):
                    image.paste(tiles[ref[offset + j]], (j % side * 32, j // side * 32))
                # High-detail positions match PC source frames exactly. Lower
                # levels also accept the native PC nearest-neighbour mip.
                targets = []
                candidates = [image]
                if level == 128:
                    candidates = [old]
                else:
                    candidates += [old.resize((level, level), Image.Resampling.NEAREST)]
                    candidates += [old.crop((0, 0, 128, 128)).resize((level, level), Image.Resampling.BOX)]
                for candidate in candidates:
                    targets.extend(t for t in positions(candidate) if Path(t[0]).stem.upper() == f'NRACE{number:02d}')
                source = {'wad': track + '/TRACK.WAD', 'entry': 'library.cmp', 'table': 'library.ttf', 'material': index, 'level': level}
                if level == 128:
                    # The native allocator places 128x128 road materials in
                    # alternating top/bottom slots, advancing x by 128. Some
                    # prepared TGA copies differ from even TRACKTEX.CMP; use
                    # this verified allocator layout rather than guessing a
                    # visually similar rectangle.
                    fixed = [(name, index // 2 * 128, index % 2 * 128) for name, atlas in native.items() if Path(name).stem.upper() == f'NRACE{number:02d}']
                    targets = fixed
                add(image, source, f'{track}/road/{index}/{level}', list(dict.fromkeys(targets)))
        audit.append({'source': track + '/LIBRARY.CMP + LIBRARY.TTF', 'selection': 'pc_byte_identical', 'road_materials': len(pc_road)})

    # Audit every PS1 archive now. No install-time PC/PS1 byte comparison.
    for path in sorted(args.ps1_root.rglob('*')):
        if path.suffix.lower() not in ['.cmp', '.tim']:
            continue
        name = path.relative_to(args.ps1_root).as_posix().upper()
        data = path.read_bytes()
        identical = pc_files.get(name) == data
        selections.append({'file': name, 'selection': 'pc_byte_identical' if identical else 'ps1', 'sha256': sha(data), 'pc_sha256': sha(pc_files[name]) if name in pc_files else None})
        frames = unpack_cmp(data) if path.suffix.lower() == '.cmp' else [data]
        if path.parent.name.upper().startswith('TRACK') and path.stem.lower() not in ['scene', 'sky']:
            continue
        matched = 0
        for index, frame in enumerate(frames):
            if not frame:
                continue
            image = decode_tim(frame)[0]
            source = pc_tim.get(sha(frame))
            if source is not None:
                assert pc_tim_bytes[sha(frame)] == frame
            if source is None:
                source = {'ps1': name, 'frame': index}
            mapped = add(image, source, f'{name}/{index}')
            matched += mapped
            if not positions(image) and image.width >= 8 and image.height >= 8:
                remaining.append({'source': name, 'frame': index, 'size': image.size})
        audit.append({'source': name, 'selection': 'pc_byte_identical' if identical else 'ps1', 'native_placements': matched})
        print(name, matched, flush=True)

    plan = {'format': 1, 'revision': 1, 'edition': 'WipEout 2097 Europe SLES-00327', 'policy': 'Fixed project-time selection: PS1 unless native bytes are identical; keep PC-only assets and authored HD UI.', 'archives': [{k:v for k,v in s.items() if k in ['file','selection']} for s in selections], 'operations': operations}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # The installer applies a sheet's placements only to this exact sheet.
    used = list(dict.fromkeys(op['atlas'] for op in operations))
    corrected, patches = layout_corrections(plan['operations'])
    # With the PlayStation disc imported its track archives replace the PC
    # ones, and these sheets are laid out as the archive's TIM headers say.
    # Not Talon's Reach: its model is the same on both, and the PlayStation
    # layout puts its smoke texture where the PC game keeps its own effects.
    scenery = [{'track': track, 'atlases': [name for name in used if Path(name).stem.upper() == f'NRACE{number:02d}']}
               for number, track in enumerate(race_order, 1) if track != 'TRACK01']
    plan = {**{k: v for k, v in plan.items() if k != 'operations'},
            'scenery': scenery,
            'patches': patches,
            'sheets': [{'file': name, 'sha256': native_sha[name]} for name in used],
            'operations': corrected}
    args.output.write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
    reused = [op for op in operations if 'ps1' not in op['source'] and 'material' not in op['source']]
    proof = {'pc_region_sources_verified_byte_for_byte': len(reused),
             'distinct_reused_ps1_tim_frames': len({op['label'] for op in reused}),
             'road_libraries_and_layouts_verified_byte_for_byte': len(wads) * 2}
    report = {'archive_byte_audit': selections, 'selection': audit, 'native_placements': len(operations), 'not_located_in_pc_atlases': remaining, 'byte_verification': proof}
    audit_path = args.audit or args.output.with_suffix('.audit.json')
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'operations': len(operations), 'unlocated': len(remaining), 'output': str(args.output)}))


if __name__ == '__main__':
    main()
