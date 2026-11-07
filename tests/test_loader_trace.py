"""Pin the loader facts. Originals are read, never written."""

import unittest
from pathlib import Path

from tools.audit_cache import (
    CALL_MODE_ZERO_VA,
    CEK_STRING_VA,
    GOT_VA,
    PIPELINE_VA,
    audit,
    check_bootstrap,
    cstr,
    read_bootstrap,
    s32,
)

ROOT = Path(__file__).resolve().parents[1]
APK = ROOT / "investigation" / "original" / "DoubleDragon.apk"
CACHE = ROOT / "investigation" / "original" / "RuntimeApps" / "gg" / "1327"


class LoaderTraceTest(unittest.TestCase):
    def test_bootstrap_trace_matches_elf(self):
        boot = read_bootstrap(APK)
        lines = check_bootstrap(boot)
        self.assertTrue(any("/ro/cek" in line for line in lines))
        self.assertEqual(cstr(boot, CEK_STRING_VA), "/ro/cek")
        self.assertEqual((GOT_VA + s32(boot, 0x861E4)) & 0xFFFFFFFF, CEK_STRING_VA)
        self.assertEqual(boot[PIPELINE_VA : PIPELINE_VA + 2], b"\xf0\xb5")
        self.assertEqual(boot[CALL_MODE_ZERO_VA : CALL_MODE_ZERO_VA + 2], b"\x00\x22")

    def test_audit_reports_missing_res_and_keystore_without_claiming_decrypt(self):
        code, text = audit(APK, CACHE)
        self.assertEqual(code, 0)
        self.assertIn("res.pak: AUSENTE", text)
        self.assertIn("keystore.dat: AUSENTE", text)
        self.assertIn("datastore.dat: AUSENTE", text)
        self.assertIn("DoubleDragon.exe: OK", text)
        self.assertIn("elf=False", text)
        self.assertIn("Não decifra", text)
        exe = (CACHE / "DoubleDragon.exe").read_bytes()
        self.assertNotEqual(exe[:4], b"\x7fELF")
        self.assertGreater(len(exe), 1_000_000)


if __name__ == "__main__":
    unittest.main()
