"""Control-flow audit of the RO parser. Fixtures are not the original CEK."""

import unittest

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs

from tools.ro_audit import (
    AUDIO,
    BOOT,
    EXE,
    FALLBACK_DEVICE,
    audit_facts,
    device_id_input,
    private_key_material_absent,
)

# Second 16-byte immediate in 0x7e500, stored and then overwritten by the
# keystore-password mask before any call can consume it.
# Load order, not memory order. The first group is stored and then replaced
# by the second group (the keystore-password mask) before any call.
DEAD_WORDS = (0x26DD3EE6, 0xBC700942, 0x79F386C8, 0xBB4B79A1)
MASK_WORDS = (0xC9C69866, 0xB45AD25E, 0x91E83CF4, 0x544917FD)


def _lits(boot: bytes, start: int, end: int) -> list[int]:
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    out = []
    for ins in md.disasm(boot[start:end], start):
        if ins.mnemonic == "ldr" and "[pc" in ins.op_str:
            imm = int(ins.op_str.split("#")[-1].rstrip("]"), 0)
            out.append(int.from_bytes(boot[((ins.address + 4) & ~3) + imm :][:4], "little"))
        if ins.mnemonic == "bl" and ins.address < 0x7E87A:
            raise AssertionError(f"call before overwrite at {ins.address:#x}")
    return out


class RoAuditTest(unittest.TestCase):
    def test_device_id_fallback_is_not_a_key(self):
        boot = BOOT.read_bytes()
        facts = audit_facts(boot)
        self.assertEqual(facts["fallback_literal"], FALLBACK_DEVICE)
        self.assertTrue(facts["null_attribute_keeps_literal"])
        self.assertTrue(facts["query_attribute_literal"])
        self.assertIn("SHA256", facts["sha256_symbol"])
        self.assertEqual(device_id_input(boot, None), FALLBACK_DEVICE)
        self.assertEqual(device_id_input(boot, b"FIXTURE-DEVICE"), b"FIXTURE-DEVICE")
        # The payload SetKey is earlier than this hash, and the module key is
        # loaded from the RO output string, not from this literal.
        self.assertEqual(facts["payload_iv_length"], "movs r3, #0x10")
        self.assertEqual(facts["module_key_load"], "ldr r1, [sp, #0x280]")
        self.assertNotEqual(facts["module_key_load"], "movs r1, #0x10")

    def test_wrapping_and_branches(self):
        boot = BOOT.read_bytes()
        facts = audit_facts(boot)
        self.assertIn("OAEP", facts["oaep_decryptor_symbol"])
        self.assertIn("RSA", facts["oaep_decryptor_symbol"])
        self.assertIn("Decryptor", facts["oaep_decryptor_symbol"])
        self.assertEqual(facts["payload_skip_iv"], ("adds r1, #0x10", "subs r2, #0x10"))
        self.assertEqual(facts["module_iv_length"], "movs r3, #0x10")
        self.assertEqual(facts["mode0_site"], "movs r2, #0")
        self.assertEqual(facts["mode1_site"], "movs r2, #1")
        self.assertEqual(facts["cek_string"], b"/ro/cek")
        self.assertEqual(facts["pek_string"], b"/ro/pek")
        self.assertEqual(facts["miss_throws"], b"Failed to generate key pair!")
        self.assertEqual(facts["keystore_write_failure"], b"Could not write the keystore!")
        self.assertEqual(facts["null_attribute_sets_error"], "bl #0x587a0")
        self.assertEqual(facts["null_attribute_still_opens"], "bl #0x90f98")

    def test_second_immediate_is_overwritten_before_use(self):
        boot = BOOT.read_bytes()
        early = _lits(boot, 0x7E848, 0x7E860)
        late = _lits(boot, 0x7E87A, 0x7E88A)
        self.assertEqual(tuple(early), DEAD_WORDS)
        self.assertEqual(tuple(late), MASK_WORDS)
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
        branch = next(md.disasm(boot[0x7E866:0x7E86A], 0x7E866))
        self.assertEqual(branch.mnemonic, "blt")
        self.assertEqual(branch.op_str, "#0x7e87a")

    def test_no_embedded_private_key_and_no_recovery(self):
        boot = BOOT.read_bytes()
        exe = EXE.read_bytes()
        audio = AUDIO.read_bytes()
        hits = private_key_material_absent(boot, exe, audio)
        for name, found in hits.items():
            for needle, at in found.items():
                self.assertEqual(at, -1, f"{name} contains {needle} at {at}")
        self.assertNotEqual(exe[:4], b"\x7fELF")
        self.assertNotEqual(exe[:2], b"MZ")
        self.assertGreater(len(exe), 1_000_000)
        self.assertGreater(len(audio), 0)


if __name__ == "__main__":
    unittest.main()
