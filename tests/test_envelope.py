"""The embedded mask is a keystore password input, not a recovered module."""

import unittest
from pathlib import Path

from tools.envelope_probe import (
    AUDIO,
    EXE,
    emulate_xor,
    gcm_mask_authenticates,
    iv_parameter_name,
    mask_bytes,
    wrapped_xor,
)

BOOT = Path("/tmp/dd-work/assets/bootstrap.exe")


class EnvelopeProbeTest(unittest.TestCase):
    def test_mask_and_fixture_xor_are_not_the_module_key(self):
        boot = BOOT.read_bytes()
        exe = EXE.read_bytes()
        mask = mask_bytes(boot)
        self.assertEqual(mask.hex(), "fd1749545ed25ab46698c6c9f43ce891")
        empty = emulate_xor(boot, b"")
        self.assertEqual(empty["derived"], mask)
        self.assertEqual(empty["path"], "/data/keystore.dat")
        fixture = b"FIXTURE-DEVICE!"
        mixed = emulate_xor(boot, fixture)
        self.assertEqual(mixed["derived"], wrapped_xor(mask, fixture))
        self.assertNotEqual(mixed["derived"], mask)
        self.assertEqual(iv_parameter_name(boot), b"IV")
        self.assertEqual(boot[0x8F30C:0x8F30E], b"\x20\x21")  # movs r1, #0x20
        self.assertEqual(boot[0x86928:0x8692A], b"\x10\x22")  # movs r2, #0x10
        self.assertFalse(gcm_mask_authenticates(exe, mask))
        self.assertFalse(gcm_mask_authenticates(AUDIO.read_bytes(), mask))
        self.assertNotEqual(exe[:4], b"\x7fELF")


if __name__ == "__main__":
    unittest.main()
