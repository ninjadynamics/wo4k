"""Offline audit decoder. Not shipped as a game dependency."""
import hashlib
import struct
from PIL import Image

def sha(data):
    return hashlib.sha256(data).hexdigest()


def u16(data, at):
    return struct.unpack_from('<H', data, at)[0]


def u32(data, at):
    return struct.unpack_from('<I', data, at)[0]


def unpack_cmp(data):
    count = u32(data, 0)
    assert 0 < count <= 4096 and 4 + 4*count <= len(data)
    sizes = [u32(data, 4+i*4) for i in range(count)]
    assert all(0 <= s <= 16777216 for s in sizes) and sum(sizes) <= 67108864
    bit = (4 + count*4)*8
    def read(n):
        nonlocal bit
        value = 0
        for _ in range(n):
            assert bit < len(data)*8, 'Truncated CMP'
            value = (value << 1) | ((data[bit >> 3] >> (7-(bit & 7))) & 1)
            bit += 1
        return value
    window, pos, decoded = bytearray(8192), 1, bytearray()
    while True:
        if read(1):
            chunk = bytes([read(8)])
        else:
            start = read(13)
            if start == 0:
                break
            length = read(4)+3
            # Overlapping matches must read from the evolving sliding window.
            for i in range(length):
                value = window[(start+i) & 8191]
                decoded.append(value)
                window[pos] = value
                pos = (pos+1) & 8191
            assert len(decoded) <= sum(sizes), 'CMP overflow'
            continue
        for value in chunk:
            decoded.append(value)
            window[pos] = value
            pos = (pos+1) & 8191
        assert len(decoded) <= sum(sizes), 'CMP overflow'
    assert len(decoded) == sum(sizes), 'CMP length mismatch'
    frames, at = [], 0
    for size in sizes:
        frames.append(bytes(decoded[at:at+size]))
        at += size
    return frames


def decode_tim(data, palette_no=0):
    assert len(data) >= 20 and u32(data, 0) == 16, 'Invalid TIM'
    flags, at, palette, palette_count, clut = u32(data, 4), 8, [], 0, None
    mode = flags & 7
    assert mode <= 3, 'Unsupported TIM mode'
    if flags & 8:
        size = u32(data, at)
        assert size >= 12 and at+size <= len(data)
        pw, ph = u16(data, at+8), u16(data, at+10)
        colors = 16 if mode == 0 else 256
        assert pw*ph*2 <= size-12 and pw*ph >= colors
        palette_count = pw*ph // colors
        assert 0 <= palette_no < palette_count
        palette = [u16(data, at+12+(palette_no*colors+i)*2) for i in range(colors)]
        clut = {'x': u16(data, at+4), 'y': u16(data, at+6), 'width': pw, 'height': ph}
        at += size
    size, words, height = u32(data, at), u16(data, at+8), u16(data, at+10)
    width = (words*4, words*2, words, words*2//3)[mode]
    assert size >= 12 and at+size <= len(data) and words*height*2 <= size-12
    assert width > 0 and height > 0 and width*height <= 16777216
    rgb, alpha = bytearray(), bytearray()
    stp = black_stp = near_black = 0
    for y in range(height):
        row = at+12+y*words*2
        for x in range(width):
            if mode == 3:
                color = tuple(data[row+x*3:row+x*3+3])
            else:
                if mode == 2:
                    pixel = u16(data, row+x*2)
                else:
                    assert palette, 'Missing TIM palette'
                    index = (data[row+x//2] >> ((x&1)*4)) & 15 if mode == 0 else data[row+x]
                    pixel = palette[index]
                color = tuple(((pixel >> shift) & 31)*255//31 for shift in (0, 5, 10))
                stp += bool(pixel & 32768)
                black_stp += pixel == 32768
            rgb.extend(color)
            # PC black-key preview only; no inferred soft alpha or cleanup.
            alpha.append(255 if any(color) else 0)
            near_black += 0 < max(color) < 32
    opaque = Image.frombytes('RGB', (width, height), bytes(rgb))
    masked = opaque.convert('RGBA')
    masked.putalpha(Image.frombytes('L', (width, height), bytes(alpha)))
    metadata = {'width': width, 'height': height, 'bpp': (4, 8, 16, 24)[mode],
                'vram_x_words': u16(data, at+4), 'vram_y': u16(data, at+6),
                'palette_count': palette_count, 'clut': clut,
                'black_pixels': alpha.count(0), 'near_black_nonzero_pixels': near_black,
                'stp_bit_pixels': stp, 'black_with_stp_bit_pixels': black_stp,
                'rgb_sha256': sha(bytes(rgb)), 'tim_sha256': sha(data)}
    return opaque, masked, metadata
