"""VP9 alpha decoding and one stable crop per complete animation."""
import subprocess
from PIL import Image, ImageChops
def decode(ffmpeg, source, fps):
    raw = subprocess.run([
        ffmpeg, "-v", "error", "-c:v", "libvpx-vp9", "-i", str(source),
        "-vf", f"fps={fps},scale=320:180:flags=lanczos", "-f", "rawvideo",
        "-pix_fmt", "rgba", "pipe:1",
    ], check=True, stdout=subprocess.PIPE).stdout
    stride = 320 * 180 * 4
    if not raw or len(raw) % stride:
        raise ValueError(f"Invalid decoded frames: {source}")
    return [Image.frombytes("RGBA", (320, 180), raw[i:i + stride])
            for i in range(0, len(raw), stride)]


def prepare(frames, size):
    # One crop for the entire motion: no frame-by-frame size changes or jitter.
    union = Image.new("L", frames[0].size)
    for frame in frames:
        union = ImageChops.lighter(union, frame.getchannel("A"))
    bounds = union.point(lambda a: 255 if a >= 96 else 0).getbbox()
    if bounds is None:
        raise ValueError("Animation has no visible character")
    x0, y0, x1, y1 = bounds
    edge = max(x1 - x0, y1 - y0) + 10
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    crop = (cx - edge // 2, cy - edge // 2,
            cx - edge // 2 + edge, cy - edge // 2 + edge)
    return [frame.crop(crop).resize((size, size), Image.Resampling.LANCZOS)
            for frame in frames]
