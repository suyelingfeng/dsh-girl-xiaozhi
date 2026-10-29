"""Build transparent 30 Hz DSA1 animations and documentation GIF previews.

Requires Pillow and FFmpeg with libvpx-vp9 (the native VP9 decoder loses alpha).
Sources are kept locally so subsequent builds do not depend on a dsh installation.
"""
import argparse
import json
import shutil
from pathlib import Path

from PIL import Image, ImageDraw
from source import decode, prepare
from preview import save_gif
from codec import encode, decode as verify_decode
from palette import quantize

ROOT = Path(__file__).resolve().parent
# Keep full 30 Hz motions within the 8 MiB assets slot using XOR compression.
# The board scales this smaller decode canvas back to a 176 px character.
SIZE = 160
FPS = 30
COLORS = 32
# Each source is a complete motion, including its return to the resting pose.
ACTIONS = {
    "idle": "待机呼吸休闲",
    "happy": "点击回应-开心跃动",
    "laughing": "点击回应-挠痒咯咯笑",
    "angry": "点击回应-傲娇生气",
    "shy": "点击回应-害羞惊讶",
    "wave": "点击回应-元气挥手",
    "sad": "工作状态-垂头叹气冒汗",
    "surprised": "被吓一跳",
    "sleepy": "原地小憩沉眠",
    "thinking": "工作状态-思考冒泡",
    "delicious": "大口吃零食",
    "loving": "女仆屈膝礼仪",
}
EMOTIONS = {
    "neutral": "idle", "relaxed": "idle", "robot_2": "idle",
    "happy": "happy", "laughing": "laughing", "funny": "laughing",
    "silly": "laughing", "cool": "wave", "confident": "wave",
    "winking": "wave", "delicious": "delicious", "sad": "sad",
    "crying": "sad", "angry": "angry", "surprised": "surprised",
    "shocked": "surprised", "sleepy": "sleepy", "thinking": "thinking",
    "confused": "thinking", "loving": "loving", "kissy": "loving",
    "embarrassed": "shy",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--import-from", type=Path,
                        help="Installed dsh-pet package (copies only selected assets + license)")
    parser.add_argument("--out", type=Path, default=ROOT.parents[1] / "main/assets/dsh-girl")
    parser.add_argument("--preview-out", type=Path, default=ROOT.parents[1] / "docs/dsh-girl")
    args = parser.parse_args()
    sources = ROOT / "sources"
    sources.mkdir(exist_ok=True)
    if args.import_from:
        for name in ACTIONS.values():
            shutil.copy2(args.import_from / "assets/webm" / f"{name}.webm", sources)
        shutil.copy2(args.import_from / "LICENSE", sources / "LICENSE")
    args.out.mkdir(parents=True, exist_ok=True)
    args.preview_out.mkdir(parents=True, exist_ok=True)
    previews = {}
    metadata = {}
    for action, source in ACTIONS.items():
        frames = prepare(decode(args.ffmpeg, sources / f"{source}.webm", FPS), SIZE)
        targets = [emotion for emotion, value in EMOTIONS.items() if value == action]
        destination = args.out / f"{targets[0]}.dsa"
        indexed, palette = quantize(frames, COLORS)
        packed = encode(indexed, palette, FPS)
        # Round-trip every pixel of every frame before replacing any assets.
        decoded = verify_decode(packed)[4]
        if decoded != [frame.tobytes() for frame in indexed]:
            raise ValueError(f"DSA1 round-trip mismatch: {source}")
        destination.write_bytes(packed)
        for emotion in targets[1:]:
            shutil.copy2(destination, args.out / f"{emotion}.dsa")
        previews[action] = frames[len(frames) // 3]
        metadata[action] = {"source": source + ".webm", "emotions": targets,
                            "frames": len(frames), "bytes": destination.stat().st_size}
        print(action, metadata[action], flush=True)
    # Remove only the superseded static files after every animation succeeds.
    for emotion in EMOTIONS:
        (args.out / f"{emotion}.png").unlink(missing_ok=True)
        (args.out / f"{emotion}.gif").unlink(missing_ok=True)
    shutil.copy2(sources / "LICENSE", args.out / "LICENSE.dsh-pet")
    (ROOT / "pet-animations.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8", newline="\n")
    sheet = Image.new("RGB", (SIZE * 4, (SIZE + 24) * 3), "#eaf0f9")
    draw = ImageDraw.Draw(sheet)
    for i, (action, frame) in enumerate(previews.items()):
        x, y = i % 4 * SIZE, i // 4 * (SIZE + 24)
        sheet.paste(frame, (x, y), frame)
        draw.text((x + 8, y + SIZE + 3), action, fill="#25345c")
    sheet.save(args.preview_out / "pet-contact-sheet.png")
    # GIF is a documentation preview only; firmware uses DSA1 delta frames.
    idle = prepare(decode(args.ffmpeg, sources / f"{ACTIONS['idle']}.webm", FPS), SIZE)
    save_gif(idle, args.preview_out / "pet-idle-preview.gif", SIZE, FPS, COLORS)


if __name__ == "__main__":
    main()
