"""Real DSA1 assets: complete XOR/zlib decode, alpha and alias compatibility."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('dsh_codec', ROOT / 'scripts/dsh_pet/codec.py')
CODEC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CODEC)


class DshAnimationTest(unittest.TestCase):
    def test_all_real_motions_decode_at_thirty_hz_with_transparency(self):
        metadata = json.loads((ROOT / 'scripts/dsh_pet/pet-animations.json').read_text(encoding='utf-8'))
        emotions = set()
        for action in metadata.values():
            canonical = ROOT / 'main/assets/dsh-girl' / (action['emotions'][0] + '.dsa')
            packed = canonical.read_bytes()
            width, height, fps, palette, frames = CODEC.decode(packed)
            self.assertEqual((width, height, fps), (160, 160, 30))
            self.assertEqual(len(frames), action['frames'])
            self.assertEqual(len(packed), action['bytes'])
            self.assertGreater(len(set(frames)), 1)
            self.assertEqual(palette[-4:], bytes(4))
            transparent = len(palette) // 4 - 1
            for frame in frames:
                self.assertEqual((frame[0], frame[width-1], frame[-width], frame[-1]),
                                 (transparent,) * 4)
            for emotion in action['emotions']:
                emotions.add(emotion)
                self.assertEqual((canonical.parent / (emotion + '.dsa')).read_bytes(), packed)
        self.assertEqual(len(metadata), 12)
        self.assertEqual(len(emotions), 22)

    def test_reference_decoder_rejects_corrupt_frame_size(self):
        # One pixel, one palette entry, but a frame inflating to two pixels.
        import zlib
        compressed = zlib.compress(bytes(2))
        packed = struct.pack('<4sHHHHI', b'DSA1', 1, 1, 30, 1, 1)
        packed += bytes(4) + struct.pack('<II', 0, len(compressed)) + compressed
        with self.assertRaisesRegex(ValueError, 'frame size'):
            CODEC.decode(packed)


if __name__ == '__main__':
    unittest.main()
