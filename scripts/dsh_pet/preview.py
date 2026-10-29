"""Documentation-only GIF preview; never packed into the firmware."""
from PIL import Image
def save_gif(frames, path, size, fps, colors):
    # Shared palette prevents color flicker. Last index is always transparent.
    sample = Image.new("RGB", (size * 10, size))
    for i in range(10):
        sample.paste(frames[i * (len(frames) - 1) // 9].convert("RGB"), (i * size, 0))
    palette = sample.quantize(colors=colors - 1, method=Image.Quantize.MEDIANCUT)
    # Pad unused entries with an existing color so opaque pixels cannot map to
    # the reserved transparency entry during palette quantization.
    colors = palette.getpalette()[:(colors - 1) * 3]
    palette.putpalette(colors + colors[:3])
    indexed = []
    for frame in frames:
        quantized = frame.convert("RGB").quantize(palette=palette, dither=Image.Dither.NONE)
        mask = frame.getchannel("A").point(lambda a: 255 if a < 96 else 0)
        quantized.paste(colors - 1, mask=mask)
        indexed.append(quantized)
    # GIF delays use 10 ms units; distribute rounding instead of speeding up
    # an entire ten-second clip at frame rates that do not divide 100.
    delays = [10 * (round((i + 1) * 100 / fps) - round(i * 100 / fps))
              for i in range(len(indexed))]
    indexed[0].save(path, save_all=True, append_images=indexed[1:],
                    duration=delays, loop=0, transparency=colors - 1,
                    background=colors - 1, disposal=2, optimize=False,
                    palette=bytes(palette.getpalette()))
