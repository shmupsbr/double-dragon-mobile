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

    def test_jump_rises_and_lands_in_act_five(self):
        apex = self.dump(6, "jump")
        down = self.dump(18, "jump")
        self.assertIn("pm 11", apex)
        self.assertIn("y 191", apex)
        self.assertIn("pm 0", down)
        self.assertIn("y 239", down)

    def test_close_kick_is_reaction_five(self):
        # Labeled fixture: one foe standing inside the 90px close-kick test.
        text = self.dump(8, "kick")
        self.assertIn(" 25 x", text)
        self.assertIn("m 0 0 2", text)

    def test_directed_jump_keeps_vx_and_accel_as_separate_signs(self):
        # ciBillyMoveData +0x18 is vx -393216 and +0x1c is accel +16384.
        # Facing right negates both. One MoveX tick is 6px. Y is not mirrored.
        right = self.dump(2, "jumpr").splitlines()[0]
        left = self.dump(2, "jumpl").splitlines()[0]
        self.assertEqual(right, "x 66 z 56 act 0 frame 0 facing 1")
        self.assertEqual(left, "x 54 z 56 act 0 frame 0 facing 0")

    def test_double_tap_dash_moves_sixteen_pixels(self):
        # Record +0x28 is X vel -1048576. Right negates it. The second tap
        # is frame 8, so frame 9 has not stepped yet and frame 12 has three.
        armed = self.dump(9, "dash").splitlines()[0]
        moved = self.dump(12, "dash").splitlines()[0]
        self.assertEqual(armed, "x 65 z 56 act 0 frame 0 facing 1")
        self.assertIn("x 113 ", moved)
        self.assertIn("facing 1", moved)

    def test_button7_up_is_reaction_five(self):
        # Act 50, record 14, damage 12. Enemy scale adds half: 40 becomes 22,
        # and reaction 5 is the same knockdown as a close kick.
        up = self.dump(8, "b7")
        self.assertIn("hp 100 40 30 22", up)
        self.assertIn("m 0 0 2", up)

    def test_button7_down_is_reaction_eight(self):
        # Fixture, not the route. Act 48's hit flag is not on the first
        # frames, so frame 2 still has 40 HP. Frame 3 stores the launch.
        # Record byte +0x0f is 3 and +0x10 is 1, so facing right keeps
        # record 3's +131072 (2px) and facing left negates it.
        windup = self.dump(2, "b7d")
        launched = self.dump(3, "b7d")
        right = self.dump(4, "b7d")
        left = self.dump(4, "b7l")
        self.assertIn("hp 100 40 30 40", windup)
        self.assertIn("hp 100 40 30 18", launched)
        self.assertIn("m 0 0 17", launched)
        self.assertIn("x 500 500 72", right)
        self.assertIn("x 500 500 48", left)

    def test_reaction_eight_gets_up_when_hp_remains(self):
        # Same fixture. Six MoveX ticks land in GT_Down (mode 18).
        # Act 41 then GT_Okiagari (mode 19) and ReadySet. HP stays 18.
        down = self.dump(9, "b7d")
        rising = self.dump(40, "b7d")
        ready = self.dump(45, "b7d")
        self.assertIn("m 0 0 18", down)
        self.assertIn("x 500 500 82", down)
        self.assertIn("hp 100 40 30 18", rising)
        self.assertIn("m 0 0 19", rising)
        self.assertIn("m 0 0 0", ready)
        self.assertIn("hp 100 40 30 18", ready)

    def test_reaction_eight_kos_at_zero_hp(self):
        # Fixture spawned at 22 HP, so the scaled 22 damage reaches 0.
        # The body still launches and lies down, then GT_KO unlinks it.
        air = self.dump(3, "b7k")
        ko = self.dump(40, "b7k")
        gone = self.dump(60, "b7k")
        self.assertIn("hp 100 40 30 0", air)
        self.assertIn("m 0 0 17", air)
        self.assertIn("m 0 0 20", ko)
        self.assertIn("m 0 0 4", gone)

    def test_wall_entry_ejects_to_the_cell_edge_then_walks_out(self):
        # First velocity tick is 76 to 74, still inside the format-0 row,
        # so FloorWallCheck stores (Z & ~15) - 1 = 63. Ten later ticks of
        # -2px land on the sidewalk at 43.
        first = self.dump(2, "entry")
        done = self.dump(12, "entry")
        self.assertIn("fz 56 8 63", first)
        self.assertIn("fz 56 8 43", done)

    def test_scripted_kicks_open_the_door(self):
        # Kick only. No quota hook. Phase becomes 1 at frame 1096, while
        # the extra-10 enemy is still absent. He is in the list at 1100.
        opening = self.dump(1096, "route")
        spawned = self.dump(1100, "route")
        self.assertIn("ph 1", opening)
        self.assertIn("nf 9", opening)
        self.assertIn("ph 1", spawned)
        self.assertIn("nf 10", spawned)

    def test_scripted_route_dies_before_the_phase2_wall(self):
        # The door pair is still at 40 and 30 when Jimmy reaches 0 HP.
        # Phase stays 1. This is the measured input, not a forced lock.
        text = self.dump(1600, "route")
        self.assertIn("hp 0 ", text)
        self.assertIn("pm 4", text)
        self.assertIn("ph 1", text)
        self.assertIn("40 30", text)

    def test_player_loss_stays_down(self):
        text = self.dump(700, "crowd")
        self.assertIn("hp 0 40 30", text)
        self.assertIn("pm 4", text)

if __name__ == "__main__":
    unittest.main()
