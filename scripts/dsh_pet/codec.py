"""DSA1: shared ARGB palette plus independently zlib-compressed XOR frames.

Transparent pixels are palette entries too, so moving silhouettes erase cleanly.
Offsets allow bounds checks before decompression. Each loop starts from index 0.
All integers are little endian; frame deadlines derive from frame index / FPS.
"""
import struct
import zlib


def encode(frames, palette, fps):
    width, height = frames[0].size
    colors = len(palette) // 4
    previous = bytes(width * height)
    payload = bytearray()
    offsets = [0]
    for frame in frames:
        current = frame.tobytes()
        delta = bytes(a ^ b for a, b in zip(current, previous))
        payload.extend(zlib.compress(delta, level=9))
        offsets.append(len(payload))
        previous = current
    header = struct.pack('<4sHHHHI', b'DSA1', width, height, fps, colors, len(frames))
    return header + palette + struct.pack(f'<{len(offsets)}I', *offsets) + payload


def decode(data):
    """Independent host reference decoder for round-trip asset verification."""
    magic, width, height, fps, colors, count = struct.unpack_from('<4sHHHHI', data)
    if magic != b'DSA1' or not (1 <= colors <= 256 and 1 <= fps <= 60 and count):
        raise ValueError('Invalid DSA1 header')
    palette = data[16:16 + 4 * colors]
    table = 16 + 4 * colors
    offsets = struct.unpack_from(f'<{count + 1}I', data, table)
    start = table + 4 * (count + 1)
    if offsets[0] != 0 or start + offsets[-1] != len(data):
        raise ValueError('Invalid DSA1 offsets')
    previous = bytes(width * height)
    decoded = []
    for left, right in zip(offsets, offsets[1:]):
        if left >= right:
            raise ValueError('Non-monotonic DSA1 offsets')
        delta = zlib.decompress(data[start + left:start + right])
        if len(delta) != width * height:
            raise ValueError('Invalid DSA1 frame size')
        previous = bytes(a ^ b for a, b in zip(previous, delta))
        if max(previous) >= colors:
            raise ValueError('Invalid DSA1 palette index')
        decoded.append(previous)
    return width, height, fps, palette, decoded
