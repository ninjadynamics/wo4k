"""Smoke-check the local debander on generated fixtures, never game artwork.

Requires Pillow and numpy for verification only. The deband executable itself
does not depend on Python. Run from the repository root after building it
(see LOCAL.md).
"""
import hashlib
import json
import shutil
import subprocess
import uuid
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    root = Path(__file__).resolve().parents[2]
    exe = root / 'tools/deband/deband.exe'
    work = root / '.work' / ('deband-test-' + uuid.uuid4().hex)
    source = work / 'fixture images & alpha'
    (source / 'nested').mkdir(parents=True)
    y, x = np.mgrid[:64, :96]
    rgba = np.stack([(x // 6 * 12 + 9) % 256, (y // 4 * 11 + 7) % 256,
                     (x * 3 + y * 7) % 256, np.take([0, 64, 128, 255], x % 4)], axis=2).astype('uint8')
    Image.fromarray(rgba).save(source / 'alpha & space.png')
    Image.fromarray(rgba).save(source / 'nested/alpha.tga')
    Image.fromarray(rgba[:, :, :3]).save(source / 'nested/opaque.bmp')
    Image.fromarray(rgba[:, :, :3]).save(source / 'nested/opaque.jpg', quality=95)
    Image.fromarray(rgba[:1, :4]).save(source / 'thin.png')
    # Enough concurrent encodes to catch an FFmpeg child inheriting another
    # worker's still-open PNG output handle on Windows (rename sharing errors).
    rng = np.random.default_rng(2097)
    for i in range(24):
        stress = rng.integers(0, 256, (256, 256, 4), dtype='uint8')
        stress[:, :, 3] = np.take([0, 64, 128, 255], np.arange(256) % 4)
        Image.fromarray(stress).save(source / f'batch-{i:02d}.png')
    before = {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in source.rglob('*') if p.is_file()}

    def run(args, success=True):
        result = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True,
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=90)
        if success and result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)
        if not success and result.returncode == 0:
            raise AssertionError('Expected failure: ' + repr(args))
        return result

    run(['-h'])
    backends = ['native'] + (['ffmpeg'] if shutil.which('ffmpeg') else [])
    for backend in backends:
        destination = work / backend
        args = (['-s'] if backend == 'native' else []) + ['-j', '4', '-o', destination, source]
        result = run(args)
        (work / f'{backend}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        assert f'{len(before)} succeeded, 0 failed' in result.stdout
        if backend == 'ffmpeg':
            assert 'Backend: FFmpeg' in result.stdout
            assert 'WARN:' not in result.stderr, result.stderr
        for path in source.rglob('*'):
            if not path.is_file():
                continue
            relative = path.relative_to(source)
            output = destination / relative
            if path.suffix.lower() != '.png':
                output = output.with_name(output.name + '.png')
            with Image.open(path) as a, Image.open(output) as b:
                assert a.size == b.size, relative
                ar, br = np.array(a.convert('RGBA')), np.array(b.convert('RGBA'))
                assert np.array_equal(ar[:, :, 3], br[:, :, 3]), relative
                transparent = ar[:, :, 3] == 0
                assert np.array_equal(ar[transparent], br[transparent]), relative
        # Existing outputs must fail without changing either inputs or outputs.
        hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in destination.rglob('*.png')}
        run(args, success=False)
        assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == digest for p, digest in hashes.items())

    assert all(hashlib.sha256((source / p).read_bytes()).hexdigest() == digest for p, digest in before.items())
    run(['-x', '-o', work / 'invalid-options', source], success=False)
    corrupt = work / 'corrupt.png'
    corrupt.write_bytes(b'not a PNG')
    run(['-s', '-o', work / 'bad-output', corrupt], success=False)
    print(json.dumps(dict(fixtures=len(before), backends=backends, alpha_and_hidden_rgb_preserved=True,
                          sources_unchanged=True, existing_outputs_protected=True,
                          invalid_inputs_rejected=True, logs=str(work))))


if __name__ == '__main__':
    main()
