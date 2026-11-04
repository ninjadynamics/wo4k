"""Build the local texture comparison/choice page from the fixed selection map."""
import argparse
import collections
import html
import json
import struct
from pathlib import Path
from PIL import Image


def native_tga(path):
    data = path.read_bytes()
    w, h = struct.unpack_from('<HH', data, 12)
    assert data[2] == 2 and data[16] == 16
    words = struct.unpack_from('<' + str(w*h) + 'H', data, 18+data[0])
    rgb = bytes(v for c in words for v in [(c>>10&31)*255//31, (c>>5&31)*255//31, (c&31)*255//31])
    return Image.frombytes('RGB', (w, h), rgb)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, default=Path('wobble'))
    parser.add_argument('--output', type=Path, default=Path('exports/ps1-texture-choices'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    plan = json.loads((root/'manifests/texture-selection.json').read_text())
    rip_root = root/'exports/ps1-sprite-effects'
    rip = json.loads((rip_root/'manifest.json').read_text())
    frames = {(f['source'].upper(), f['frame']): f for f in rip['frames']}
    grouped = collections.OrderedDict()
    for op in plan['operations']:
        source = op['source']
        if 'material' in source:
            if source['level'] != 128:
                continue
            track = source['wad'].split('/')[0]
            key = f'{track}/ROAD#{source["material"]:04d}'
        else:
            name, number = op['label'].rsplit('/', 1)
            key = f'{name}#{int(number):04d}'
        grouped.setdefault(key, op)
    out = args.output.resolve()
    (out/'images').mkdir(parents=True, exist_ok=True)
    names = {'TRACK01': "Talon’s Reach", 'TRACK08': 'Sagarmatha', 'TRACK13': 'Valparaiso', 'TRACK20': 'Phenitia Park',
             'TRACK02': 'Gare d’Europa', 'TRACK17': 'Odessa Keys', 'TRACK06': 'Vostok Island', 'TRACK07': 'Spilskinanke'}
    cache, items = {}, []
    for number, (key, op) in enumerate(grouped.items()):
        source = op['source']
        if 'material' in source:
            track = source['wad'].split('/')[0]
            frame = source['material']
            name = track+'/ROAD'
            image = Image.open(root/f'exports/track-texture-comparison/ps1/{track.lower()}/road/{frame:04d}.png').convert('RGB')
            group, title, default, provenance = 'Roads', names[track], 'ps1', 'PS1 colours recovered from byte-identical PC tile libraries'
        else:
            name, frame_s = key.rsplit('#', 1)
            frame = int(frame_s)
            info = frames[(name, frame)]
            image = Image.open(rip_root/info['original_rgb']).convert('RGB')
            group = 'Scenery' if name.startswith('TRACK') else ('Effects & sprites' if name.startswith('TEXTURES') else 'Shared textures')
            title = names.get(name.split('/')[0], Path(name).stem.replace('_', ' ').title())
            default = 'ps1' if 'ps1' in source else 'pc'
            provenance = 'PS1 original; native bytes differ' if default == 'ps1' else 'Verified byte-identical PC / PS1 original'
        if op['atlas'] not in cache:
            path = args.game_root/'textures/native-source'/op['atlas']
            if not path.is_file():
                path = args.game_root/'textures/sd'/op['atlas']
            cache[op['atlas']] = native_tga(path)
        pc = cache[op['atlas']].crop((op['x'], op['y'], op['x']+op['width'], op['y']+op['height']))
        assert image.size == pc.size
        stem = f'{number:04d}'
        image.save(out/'images'/f'{stem}-ps1.png')
        pc.save(out/'images'/f'{stem}-pc.png')
        items.append({'id': key, 'source': name, 'frame': frame, 'group': group, 'title': title,
                      'width': image.width, 'height': image.height, 'defaultVersion': default,
                      'pixelsIdentical': image.tobytes() == pc.tobytes(), 'provenance': provenance,
                      'ps1': f'images/{stem}-ps1.png', 'pc': f'images/{stem}-pc.png'})
    catalog = {'format': 'wo4k.texture-catalog', 'version': 1, 'catalogRevision': plan['revision'], 'textures': items}
    data = json.dumps(catalog, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/')
    template = (root/'tools/texture-picker/index.html').read_text(encoding='utf-8')
    (out/'index.html').write_text(template.replace('<!-- CATALOG -->', '<script type="application/json" id="catalog">'+data+'</script>'), encoding='utf-8')
    for name in ['app.js', 'style.css']:
        (out/name).write_bytes((root/'tools/texture-picker'/name).read_bytes())
    print(json.dumps({'page': str(out/'index.html'), 'textures': len(items), 'default_ps1': sum(i['defaultVersion']=='ps1' for i in items), 'default_pc': sum(i['defaultVersion']=='pc' for i in items)}))


if __name__ == '__main__':
    main()
