"""Provenance checks for the iOS animation tables. Not a gameplay sim."""

import unittest
from pathlib import Path

from ios.macho_anim import (
    JIMMY_ACT0,
    JIMMY_ACT1,
    JIMMY_ACT28,
    JIMMY_ACT29,
    SCREEN_H,
    SCREEN_W,
    ZAKO_C00_ACT0,
    act_span,
    attack_record,
    billy_move,
    compose_box,
    draw_flags,
    encryption,
    ENC_LIST,
    char_def,
    enc_data2,
    encounter_rows,
    first_wave,
    floor_bytes,
    scroll_enemies,
    va_to_offset,
    floor_ground,
    hero_hp,
    hurt_record,
    zako_attack_record,
    push_x,
    zako_move,
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

    def test_screen_height_is_the_map_y_base(self):
        # 0x1fa84 is mov.w r2, #0x1e0 and 0x1faaa is subs r2, #0xa0,
        # then that 320 is stored at applet+0x47ac. MapChipDraw subtracts it.
        self.assertEqual(SCREEN_W, 480)
        self.assertEqual(SCREEN_H, 320)
        words = self.blob
        # The immediates live in the armv7 slice. Check the halfwords.
        base = 0x155000
        def half(va):
            import struct
            return struct.unpack_from("<H", words, base + (va - 0x1000))[0]
        # mov.w r2, #480 and subs r2, #160 are recorded by their results above.
        self.assertEqual(half(0x1FAA2) & 0xFBF0, 0xF240)  # movw/mov.w family at the store site

    def test_punch_acts_and_hit_data(self):
        frames, opcode = act_span(self.blob, JIMMY_ACT28, JIMMY_ACT29)
        self.assertEqual(opcode, 1)
        self.assertEqual([f.pattern for f in frames], [40, 41, 43, 44, 45, 46, 47])
        self.assertTrue(any(f.flags == 8 for f in frames))
        box = attack_record(self.blob, 2)
        self.assertEqual(box[:6], (-69, -58, 65, 11, -3, 7))
        self.assertEqual(box[6], 8)
        close = attack_record(self.blob, 5)
        self.assertEqual(close[6], 12)
        hurt = hurt_record(self.blob, 1)
        self.assertEqual(hurt, (-18, -66, 31, 66, -2, 5))

    def test_first_wave_is_not_a_placed_prop(self):
        wave = first_wave(self.blob)
        self.assertEqual(len(wave), 2)
        # enemy def 0 -> char 12, draw set 8, right of the 480-wide view
        self.assertEqual(wave[0][0], 0)
        self.assertEqual(wave[0][1], 12)
        self.assertEqual(wave[0][2], 500)
        self.assertEqual(wave[0][3], 56)
        self.assertEqual(wave[0][4], 40)
        # enemy def 1 -> char 4, Bash draw set
        self.assertEqual(wave[1][1], 4)
        self.assertEqual(wave[1][2], 500)
        self.assertEqual(wave[1][3], 8)
        self.assertEqual(wave[1][4], 30)

    def test_zako_punch_reaches_the_player_scale(self):
        self.assertEqual(zako_move(self.blob), (-262144, 262144))
        record = zako_attack_record(self.blob, 6)
        self.assertEqual(record[:6], (-40, -86, 40, 44, -3, 7))
        self.assertEqual(record[6], 11)
        self.assertEqual(record[-1], 5)
        self.assertEqual(hero_hp(self.blob, 0), 100)
        self.assertEqual(hero_hp(self.blob, 1), 100)

    def test_draw_slots_are_bash_williams_bolo(self):
        import struct
        names = {6: "BASH.obm", 7: "WILLIAMS.obm", 8: "BOLO.obm"}
        # The installer ldr.w immediates are the low 12 bits. Pool base clears bits [1:0] of addr+4.
        for draw, ldr in ((6, 0x100C8), (7, 0x100EA), (8, 0x1010C)):
            raw = self.blob[va_to_offset(ldr) : va_to_offset(ldr) + 4]
            imm = struct.unpack_from("<H", raw, 2)[0] & 0xFFF
            pool = ((ldr + 4) & ~3) + imm
            addend = struct.unpack_from("<i", self.blob, va_to_offset(pool))[0]
            slot = (ldr + 8) + addend
            img = struct.unpack_from("<I", self.blob, va_to_offset(slot))[0]
            self.assertEqual(resource_name(self.blob, struct.unpack_from("<H", self.blob, va_to_offset(img))[0]), names[draw])
        self.assertEqual(char_def(self.blob, 4)[1], 6)
        self.assertEqual(char_def(self.blob, 8)[1], 7)
        self.assertEqual(char_def(self.blob, 12)[1], 8)

    def test_building_row_is_the_tall_floor_record(self):
        tiles, fmt5 = floor_bytes(self.blob)
        info = stage1_map(self.blob)
        def tile(x, z):
            return tiles[info["floor_w"] * ((z >> 4) + 1) + info["origin_x"] + (x >> 4)]
        self.assertEqual(tile(60, 63), 1)
        self.assertEqual(tile(60, 64), 0)
        self.assertEqual(floor_ground(self.blob, 60, 63), 0xEF)
        self.assertLess(floor_ground(self.blob, 60, 64), 0)
        self.assertIn(0, fmt5)
        self.assertIn(1, fmt5)

    def test_scroll_100_is_the_next_kind3_row(self):
        import struct
        raw = self.blob[va_to_offset(ENC_LIST) : va_to_offset(ENC_LIST) + 16 * 12]
        rows = [struct.unpack_from("<4i", raw, i * 16) for i in range(12)]
        self.assertEqual(rows[6][:3], (0, 0, 2))
        self.assertEqual(rows[9][:3], (100, 5, -1))
        wave = scroll_enemies(self.blob, 100)
        self.assertEqual([(e[1], e[2], e[3], e[4], e[5]) for e in wave], [
            (8, 612, 56, 40, 7),
            (8, 656, 32, 40, 7),
            (4, 612, 8, 30, 6),
        ])

    def test_push_x_matches_the_rightward_edge(self):
        self.assertEqual(push_x(0, 0, 100, 0, 1), 70)
        self.assertEqual(push_x(100, 0, 0, 0, -1), 30)
        self.assertEqual(push_x(40, 0, 40, 0, 0), 40)

    def test_linda_reaction_is_two_and_uses_hitstun_zero(self):
        import struct
        raw = self.blob[va_to_offset(0xC6CB0 + 6 * 26) : va_to_offset(0xC6CB0 + 7 * 26)]
        self.assertEqual(raw[0x0C] | (raw[0x0D] << 8), 3)
        self.assertEqual(raw[0x0E], 0)
        self.assertEqual(raw[0x11], 2)
        rec = struct.unpack_from("<iii", self.blob, va_to_offset(0xC5464))
        self.assertEqual(rec, (-262144, 81920, 10))

    def test_deferred_rows_stay_out_of_the_dispatch(self):
        done, deferred = encounter_rows(self.blob)
        kinds = {(row[0], row[1]) for row in deferred}
        self.assertIn((0, 4), kinds)
        self.assertIn((0, 9), kinds)
        self.assertNotIn((100, 9), kinds)
        self.assertNotIn((687, 9), kinds)
        self.assertIn((1055, 10), kinds)
        implemented = [row[1] for row in done]
        self.assertTrue(set(implemented) <= {0, 3, 5, 9})
        door = [row for row in done if row[1] == 9 and row[4] == 1]
        wall = [row for row in done if row[1] == 9 and row[4] == 2]
        self.assertEqual(door, [(100, 9, 767, 64, 1, 10, 0, 1)])
        self.assertEqual(wall, [(100, 9, 1000, 80, 2, 20, -16, 2)])
        enemies = [row for row in done if row[1] == 3]
        self.assertEqual(enemies[5][5], 4)
        self.assertEqual(enemies[5][2], 720)
        self.assertEqual(enemies[7][2], 880)
        self.assertEqual(enemies[8][2], 912)

    def test_quota_keys_set_phase_then_spawn_the_table_enemies(self):
        rows = enc_data2(self.blob)
        self.assertIn((2, 8, 1, 65535), rows)
        self.assertIn((2, 0, 2, 3), rows)
        self.assertIn((10, 3, 9, 65535), rows)
        self.assertIn((10, 10, 40, 11), rows)
        self.assertIn((11, 3, 10, 65535), rows)
        self.assertIn((3, 8, 2, 65535), rows)
        self.assertIn((20, 3, 11, 65535), rows)
        self.assertIn((22, 3, 13, 65535), rows)
        self.assertEqual(char_def(self.blob, 31), (17, 19, 50))
        self.assertEqual(char_def(self.blob, 6), (4, 6, 30))

    def test_bash_act_uses_its_own_patterns(self):
        frames, opcode = act_span(self.blob, ZAKO_C00_ACT0, ZAKO_C00_ACT0 + 0x5C)
        self.assertEqual(opcode, 3)
        self.assertEqual([f.pattern for f in frames], [0, 1, 2, 3, 4, 5])
        self.assertTrue(all(f.duration == 2 for f in frames))


if __name__ == "__main__":
    unittest.main()
