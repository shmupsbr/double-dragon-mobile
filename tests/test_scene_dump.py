"""Headless check of the stage viewer. Skips until the scene pack exists."""

import subprocess
import unittest
from pathlib import Path

VIEWER = Path("/workspace/ios/scene_view")
SCENE = Path("/tmp/dd-view/scene.bin")


class SceneDumpTest(unittest.TestCase):
    def setUp(self):
        if not VIEWER.is_file() or not SCENE.is_file():
            self.skipTest("scene viewer or scene.bin is not built")

    def dump(self, frames, hold):
        out = Path("/tmp/dd-view") / f"check-{hold}-{frames}.ppm"
        result = subprocess.run(
            [str(VIEWER), str(SCENE), "--dump", str(out), str(frames), hold],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertTrue(out.is_file())
        self.assertGreater(out.stat().st_size, 1000)
        return result.stdout.strip()

    def test_idle_stays_at_the_spawn(self):
        self.assertEqual(self.dump(8, "none"), "x 60 z 56 act 0 frame 4 facing 1")

    def test_right_uses_the_recovered_speed(self):
        # 16 frames * 360448 / 65536 = 88, and 60 + 88 = 148.
        self.assertEqual(self.dump(16, "right"), "x 148 z 56 act 1 frame 4 facing 1")

    def test_close_punch_uses_attack_record_five(self):
        text = self.dump(100, "meet")
        lines = text.splitlines()
        self.assertEqual(lines[1], "hp 22 30")

    def test_up_changes_depth_and_keeps_idle(self):
        self.assertEqual(self.dump(4, "up"), "x 60 z 72 act 0 frame 2 facing 1")


if __name__ == "__main__":
    unittest.main()
