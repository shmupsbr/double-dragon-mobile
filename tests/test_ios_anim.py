"""Provenance checks for the iOS animation tables. Not a gameplay sim."""

import unittest
from pathlib import Path

from ios.macho_anim import (
    JIMMY_ACT0,
    compose_box,
    encryption,
    jimmy_act0,
    jimmy_tables,
    pattern_parts,
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


if __name__ == "__main__":
    unittest.main()
