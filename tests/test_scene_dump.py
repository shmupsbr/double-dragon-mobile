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

    def test_enemies_walk_in_and_stop_at_the_move_threshold(self):
        text = self.dump(240, "watch")
        self.assertIn("hp 100 40 30 x 120 120", text)

    def test_trade_uses_both_damage_arms(self):
        text = self.dump(180, "fight")
        self.assertIn("hp 82 22 12", text)

    def test_up_changes_depth_and_keeps_idle(self):
        self.assertEqual(self.dump(4, "up"), "x 60 z 72 act 0 frame 2 facing 1")


if __name__ == "__main__":
    unittest.main()
