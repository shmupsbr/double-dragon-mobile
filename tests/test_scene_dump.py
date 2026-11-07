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
        self.assertEqual(lines[0], "x 278 z 56 act 1 frame 2 facing 1")
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
        # Player priority 2 runs before enemy priority 3, so this seed
        # clears the first pair sooner than the old foe-first order.
        held = self.dump(336, "stand")
        moving = self.dump(342, "stand")
        self.assertIn("cam 0 lock 0 nf 2", held)
        self.assertIn("lock 1 nf 2", moving)

    def test_next_row_waits_for_camera_100(self):
        spawned = self.dump(360, "stand")
        self.assertIn("40 40 30", spawned)
        self.assertIn("lock 2 nf 5", spawned)

    def test_scroll_303_spawns_linda_and_williams(self):
        text = self.dump(700, "stand")
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
        # Fixture. The player task runs before the foe, so the hit frame
        # also applies the first MoveX. Record +0x0f is 3 and +0x10 is 1:
        # facing right keeps +131072, facing left negates it.
        windup = self.dump(2, "b7d")
        launched = self.dump(3, "b7d")
        left = self.dump(4, "b7l")
        self.assertIn("hp 100 40 30 40", windup)
        self.assertIn("x 500 500 70", windup)
        self.assertIn("hp 100 40 30 18", launched)
        self.assertIn("m 0 0 17", launched)
        self.assertIn("x 500 500 72", launched)
        self.assertIn("x 500 500 46", left)

    def test_reaction_eight_gets_up_when_hp_remains(self):
        # Six MoveX ticks, the last of them landing in GT_Down.
        down = self.dump(8, "b7d")
        rising = self.dump(40, "b7d")
        ready = self.dump(44, "b7d")
        self.assertIn("m 0 0 18", down)
        self.assertIn("x 500 500 82", down)
        self.assertIn("hp 100 40 30 18", rising)
        self.assertIn("m 0 0 19", rising)
        self.assertIn("m 0 0 0", ready)
        self.assertIn("hp 100 40 30 18", ready)

    def test_reaction_eight_kos_at_zero_hp(self):
        # Spawned at 22 HP. Scaled damage 22 reaches 0, and he still
        # launches before GT_KO unlinks him.
        air = self.dump(3, "b7k")
        ko = self.dump(40, "b7k")
        gone = self.dump(55, "b7k")
        self.assertIn("hp 100 40 30 0", air)
        self.assertIn("m 0 0 17", air)
        self.assertIn("x 500 500 72", air)
        self.assertIn("m 0 0 20", ko)
        self.assertIn("m 0 0 4", gone)

    def test_crouch_holds_act_26_then_returns(self):
        # Button 9. NEXT consumes act 25 on the entry tick. Act 26 is
        # 45 frames. A direction is MoveCheck, so the crouch ends.
        held = self.dump(45, "crouch")
        done = self.dump(46, "crouch")
        walked = self.dump(6, "cwalk")
        self.assertIn("pm 21", held)
        self.assertIn("pm 0", done)
        self.assertIn("pm 0", walked)
        self.assertNotIn("x 60 ", walked.splitlines()[0])

    def test_wall_entry_ejects_to_the_cell_edge_then_walks_out(self):
        # First velocity tick is 76 to 74, still inside the format-0 row,
        # so FloorWallCheck stores (Z & ~15) - 1 = 63. Ten later ticks of
        # -2px land on the sidewalk at 43.
        first = self.dump(2, "entry")
        done = self.dump(12, "entry")
        self.assertIn("fz 56 8 63", first)
        self.assertIn("fz 56 8 43", done)

    def test_door_spawn_takes_a_real_kick_and_opens_the_wall(self):
        # Same authentic spawn as the old route (no placement, HP, flag,
        # or quota edit). The approach stays 10px off their Z, outside the
        # zako punch test, until X is inside the kick, then steps in and kicks.
        # One close kick is 10, scaled to 15: 40 becomes 25 and 30 becomes 15.
        # Both deaths are what set phase 2 and the x=992 wave.
        fresh = self.dump(1140, "route")
        kicked = self.dump(1160, "route")
        wall = self.dump(1400, "route")
        self.assertIn("ph 1", fresh)
        self.assertIn("40 30", fresh)
        self.assertIn("25 15", kicked)
        self.assertIn("ph 1", kicked)
        self.assertIn("ph 2", wall)
        self.assertIn("992", wall)

    def test_player_loss_stays_down(self):
        text = self.dump(700, "crowd")
        self.assertIn("hp 0 40 30", text)
        self.assertIn("pm 4", text)

if __name__ == "__main__":
    unittest.main()
