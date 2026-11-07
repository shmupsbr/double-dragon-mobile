"""Provenance checks for the iOS animation tables. Not a gameplay sim."""

import unittest
from pathlib import Path

from ios.macho_anim import (
    JIMMY_ACT0,
    JIMMY_ACT1,
    ZAKO_C00_ACT0,
    act_span,
    billy_move,
    compose_box,
    draw_flags,
    encryption,
    floor_ground,
    jimmy_act0,
    jimmy_tables,
    key_config,
    pattern_parts,
    player_char,
    read_i16,
    resource_name,
    stage1_map,
    stage1_spawn,
)

BINARY = Path("/tmp/ios-work/binary/ddiPhone")


class IosAnimTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file():
            raise unittest.SkipTest("sanitized ddiPhone is not at " + str(BINARY))
        cls.blob = BINARY.read_bytes()

    def test_fairplay_is_inactive(self):
        rows = encryption(self.blob)
        self.assertEqual(len(rows), 2)
        for row in rows:
            self.assertEqual(row["cryptid"], 0)
            self.assertEqual(row["cryptoff"], 0x1000)
            self.assertGreater(row["cryptsize"], 0)

    def test_jimmy_pose_is_several_rects(self):
        rects, pat, points = jimmy_tables(self.blob)
        self.assertEqual(len(rects), 864)
        first = rects[0]
        self.assertEqual((first.x, first.y, first.w, first.h), (40, 84, 39, 42))
        self.assertEqual((first.px, first.py), (23, 75))
        parts = pattern_parts(pat, points, 0)
        self.assertEqual(parts, [0, 1, 2, 3])
        x0, y0, x1, y1 = compose_box(rects, parts)
        tallest = max(rects[i].h for i in parts)
        self.assertGreater(y1 - y0, tallest)

    def test_act0_frame_list(self):
        frames, opcode = jimmy_act0(self.blob)
        self.assertEqual(JIMMY_ACT0, 0xA9FCC)
        self.assertEqual(opcode, 3)  # Act_AI_LOOP
        self.assertEqual(
            [(f.duration, f.flags, f.pattern) for f in frames],
            [(2, 0, 0), (1, 0, 1), (2, 0, 2), (1, 0, 3), (2, 0, 4)],
        )

    def test_walk_act_and_idle_return(self):
        # GT_FMoveSet passes act 1. GT_ReadySet passes act 0 when no direction.
        frames, opcode = act_span(self.blob, JIMMY_ACT1, JIMMY_ACT1 + 0x50)
        self.assertEqual(opcode, 3)
        self.assertEqual(
            [(f.duration, f.flags, f.pattern) for f in frames],
            [(2, 0, 5), (1, 0, 6), (1, 0, 7), (2, 0, 8), (2, 0, 9)],
        )

    def test_names_and_image_numbers(self):
        self.assertEqual(resource_name(self.blob, 16), "JIMMY_1.obm")
        self.assertEqual(resource_name(self.blob, 17), "JIMMY_2.obm")
        self.assertEqual(resource_name(self.blob, 21), "BASH.obm")
        self.assertEqual(resource_name(self.blob, 69), "AREA1_CHIP.obm")
        jimmy = read_i16(self.blob, 0x9F690, 14)
        self.assertEqual(jimmy[:7], [16, 17, 58, 60, 59, 61, 18])
        zako = read_i16(self.blob, 0x7E56A, 8)
        self.assertEqual(zako[:4], [21, 58, 59, 62])
        # Character id 1 is Jimmy: type 1, sheets 16 and 17.
        self.assertEqual(player_char(self.blob, 1), (1, 3, 16, 17))
        self.assertEqual(player_char(self.blob, 0)[2:], (13, 14))

    def test_stage_map_is_column_major(self):
        info = stage1_map(self.blob)
        self.assertEqual((info["width"], info["height"]), (97, 20))
        self.assertEqual((info["pixel_w"], info["pixel_h"]), (97 * 16, 20 * 16))
        self.assertEqual(len(info["main"]), 97 * 20)
        self.assertTrue(all(t >= 0 for t in info["main"]))
        self.assertGreater(sum(t < 0 for t in info["near"]), 0)
        # MapChipDraw: tx = id & 31, ty = id >> 5. Both stay on a 32-wide sheet.
        for tid in info["main"][::97]:
            self.assertGreaterEqual(tid & 31, 0)
            self.assertLess(tid >> 5, 32)

    def test_spawn_speed_and_ground(self):
        self.assertEqual(stage1_spawn(self.blob), (60, 56))
        self.assertEqual(billy_move(self.blob), (-360448, 262144))
        # Spawn tile uses floor record 1, whose height words are 0.
        self.assertEqual(floor_ground(self.blob, 60, 56), 0xEF)
        self.assertIsNone(floor_ground(self.blob, 60, 200))

    def test_draw_flag_and_key_masks(self):
        flags = draw_flags(self.blob)
        self.assertEqual(flags[:8], [0, 1, 2, 3, 1, 0, 3, 2])
        masks = key_config(self.blob)
        # Indexes 0..3 are the four stick bits. 6 is the 0x80 button.
        self.assertEqual(masks[:4], [4, 1, 8, 2])
        self.assertEqual(masks[6], 0x80)

    def test_bash_act_uses_its_own_patterns(self):
        frames, opcode = act_span(self.blob, ZAKO_C00_ACT0, ZAKO_C00_ACT0 + 0x5C)
        self.assertEqual(opcode, 3)
        self.assertEqual([f.pattern for f in frames], [0, 1, 2, 3, 4, 5])
        self.assertTrue(all(f.duration == 2 for f in frames))


if __name__ == "__main__":
    unittest.main()
