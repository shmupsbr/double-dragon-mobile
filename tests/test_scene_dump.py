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
        lines = self.dump(8, "none").splitlines()
        self.assertEqual(lines[0], "x 60 z 56 act 0 frame 4 facing 1")
        self.assertEqual(lines[1], "cam 0 lock 0 nf 2")

    def test_right_uses_the_recovered_speed(self):
        # 16 frames * 360448 / 65536 = 88, and 60 + 88 = 148.
        lines = self.dump(16, "right").splitlines()
        self.assertEqual(lines[0], "x 148 z 56 act 1 frame 4 facing 1")
        self.assertEqual(lines[1], "cam 0 lock 0 nf 2")

    def test_locked_camera_stays_while_the_wave_is_alive(self):
        lines = self.dump(80, "right").splitlines()
        self.assertEqual(lines[0], "x 278 z 56 act 1 frame 4 facing 1")
        self.assertEqual(lines[1], "cam 0 lock 0 nf 2")

    def test_up_stops_on_the_building_row(self):
        lines = self.dump(4, "up").splitlines()
        self.assertEqual(lines[0], "x 60 z 63 act 0 frame 2 facing 1")
        self.assertEqual(self.dump(40, "up").splitlines()[0], "x 60 z 63 act 0 frame 4 facing 1")

    def test_enemies_walk_in_and_stop_at_the_move_threshold(self):
        text = self.dump(240, "watch")
        self.assertIn("hp 100 40 30 x 120 120", text)

    def test_live_enemy_reaches_the_getup_act(self):
        text = self.dump(140, "stand")
        self.assertIn("hp 91 22 12", text)
        self.assertIn("m 6 6", text)

    def test_live_enemy_returns_to_the_command_list(self):
        text = self.dump(200, "stand")
        self.assertIn("hp 73 22 12", text)
        self.assertIn("m 0 0", text)

    def test_unlinking_the_wave_releases_the_camera(self):
        held = self.dump(480, "stand")
        moving = self.dump(485, "stand")
        self.assertIn("nf 2", held)
        self.assertIn("cam 0 lock 0", held)
        self.assertIn("m 4 4", moving)
        self.assertIn("lock 1", moving)
        self.assertIn("cam 36", moving)
        self.assertIn("nf 2", moving)

    def test_next_row_waits_for_camera_100(self):
        before = self.dump(490, "stand")
        spawned = self.dump(492, "stand")
        self.assertIn("cam 96 lock 1 nf 2", before)
        self.assertIn("hp 37 0 0 40 40 30", spawned)
        self.assertIn("x 344 376 612 656 612", spawned)
        self.assertIn("cam 120 lock 2 nf 5", spawned)

    def test_player_loss_stays_down(self):
        text = self.dump(700, "crowd")
        self.assertIn("hp 0 40 30", text)
        self.assertIn("pm 4", text)

if __name__ == "__main__":
    unittest.main()
