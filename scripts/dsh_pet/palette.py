"""Quantize all frames with one palette; reserve the final entry for alpha."""
import struct
from PIL import Image


def quantize(frames, colors=32):
    width, height = frames[0].size
    sample = Image.new('RGB', (width * 10, height))
    for i in range(10):
        sample.paste(frames[i * (len(frames) - 1) // 9].convert('RGB'), (i * width, 0))
    palette = sample.quantize(colors=colors - 1, method=Image.Quantize.MEDIANCUT)
    rgb = palette.getpalette()[:(colors - 1) * 3]
    palette.putpalette(rgb + rgb[:3])
    indexed = []
    for frame in frames:
        quantized = frame.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE)
        quantized.paste(colors - 1, mask=frame.getchannel('A').point(lambda a: 255 if a < 96 else 0))
        indexed.append(quantized)
    # Little-endian ARGB8888 is B,G,R,A in LVGL's byte-addressed framebuffer.
    argb = b''.join(struct.pack('<I', 0xff000000 | rgb[i*3] << 16 | rgb[i*3+1] << 8 | rgb[i*3+2])
                    for i in range(colors - 1)) + bytes(4)
    return indexed, argb
