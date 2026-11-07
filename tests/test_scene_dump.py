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
        self.assertEqual(lines[0], "x 278 z 56 act 1 frame 3 facing 1")
        self.assertEqual(lines[1], "cam 0 lock 0 nf 2")

    def test_up_stops_on_the_building_row(self):
        lines = self.dump(4, "up").splitlines()
        self.assertEqual(lines[0], "x 60 z 63 act 0 frame 2 facing 1")
        self.assertEqual(self.dump(40, "up").splitlines()[0], "x 60 z 63 act 0 frame 4 facing 1")

    def test_enemies_walk_in_and_stop_at_the_move_threshold(self):
        text = self.dump(240, "watch")
        self.assertIn("hp 100 40 30 x 120 150", text)

    def test_live_enemy_reaches_the_getup_act(self):
        text = self.dump(140, "stand")
        self.assertIn("hp 91 22 12", text)
        self.assertIn("m 6 6", text)
        foes = text.splitlines()[-1].split("x", 1)[1].split("m", 1)[0].split()
        self.assertGreaterEqual(abs(int(foes[0]) - int(foes[1])), 20)

    def test_live_enemy_is_still_in_the_lie(self):
        text = self.dump(200, "stand")
        self.assertIn("hp 82 4 0", text)
        self.assertIn("m 3 3", text)

    def test_unlinking_the_wave_releases_the_camera(self):
        moving = self.dump(382, "stand")
        self.assertIn("m 4 4", moving)
        self.assertIn("cam 48 lock 1 nf 2", moving)

    def test_next_row_waits_for_camera_100(self):
        before = self.dump(386, "stand")
        spawned = self.dump(387, "stand")
        self.assertIn("cam 96 lock 1 nf 2", before)
        self.assertIn("hp 64 0 0 40 40 30", spawned)
        self.assertIn("x 388 418 612 656 612", spawned)
        self.assertIn("cam 108 lock 2 nf 5", spawned)

    def test_scroll_303_spawns_linda_and_williams(self):
        text = self.dump(780, "stand")
        self.assertIn("25 40", text)
        self.assertIn("720 752", text)
        self.assertIn("lock 3 nf 7", text)
        self.assertIn("m 4 4 4 4 4 9 9", text)

    def test_linda_reaction_stuns_then_jimmy_knocks_her_down(self):
        stun = self.dump(24, "react")
        back = self.dump(30, "react")
        down = self.dump(50, "react")
        self.assertIn("hp 94", stun)
        self.assertIn("pm 10", stun)
        self.assertIn("hp 94", back)
        self.assertIn("pm 0", back)
        self.assertIn(" 7 x", down)
        self.assertIn("m 0 0 2", down)

    def test_quota_phase_spawns_the_object_enemies_and_releases_a_lock(self):
        door = self.dump(3, "chain")
        pair = self.dump(43, "chain")
        opened = self.dump(44, "chain")
        wall = self.dump(52, "chain")
        second = self.dump(92, "chain")
        macho = self.dump(132, "chain")
        unlocked = self.dump(133, "chain")
        self.assertIn("ph 1 q 2", door)
        self.assertIn("767", door)
        self.assertIn("40 30", pair)
        self.assertIn("767", pair)
        self.assertIn("ph 2 q 3", opened)
        self.assertIn(" 40 x", wall)
        self.assertIn("992", wall)
        self.assertIn("40 30 x", second)
        self.assertIn(" 50 x", macho)
        self.assertIn("992", macho)
        self.assertIn("lock 2", unlocked)
        self.assertIn("q 0", unlocked)

    def test_scripted_route_reaches_the_door_then_dies(self):
        # Real punches. The lunge spacing is input, not an HP change.
        # Frame 800 has cleared the scroll-100 quota, so lock 3 lets the
        # camera show the door. Linda and Williams are still up, and the
        # phase word is still 0. Frame 1000 is the player down.
        there = self.dump(800, "route")
        self.assertIn("lock 3", there)
        self.assertIn("pm 0", there)
        self.assertIn("25 40", there)
        self.assertIn("ph 0", there)
        down = self.dump(1000, "route")
        self.assertIn("pm 4", down)
        self.assertIn("ph 0", down)

    def test_player_loss_stays_down(self):
        text = self.dump(700, "crowd")
        self.assertIn("hp 0 40 30", text)
        self.assertIn("pm 4", text)

if __name__ == "__main__":
    unittest.main()
