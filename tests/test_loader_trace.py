"""Pin the loader facts. Originals are read, never written."""

import json
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
    pak_is_plaintext_container,
    read_bootstrap,
    s32,
)

ROOT = Path(__file__).resolve().parents[1]
APK = ROOT / "investigation" / "original" / "DoubleDragon.apk"
CACHE = ROOT / "investigation" / "cache1327"
INVENTORY = ROOT / "investigation" / "cache_inventory.json"


class LoaderTraceTest(unittest.TestCase):
    def test_bootstrap_trace_matches_elf(self):
        boot = read_bootstrap(APK)
        lines = check_bootstrap(boot)
        joined = "\n".join(lines)
        self.assertIn("/ro_response/key", joined)
        self.assertIn("keystore", joined)
        self.assertEqual(cstr(boot, CEK_STRING_VA), "/ro/cek")
        self.assertEqual((GOT_VA + s32(boot, 0x861E4)) & 0xFFFFFFFF, CEK_STRING_VA)
        self.assertEqual(boot[PIPELINE_VA : PIPELINE_VA + 2], b"\xf0\xb5")
        self.assertEqual(boot[CALL_MODE_ZERO_VA : CALL_MODE_ZERO_VA + 2], b"\x00\x22")
        self.assertEqual(boot.find(b"save.dat"), -1)
        self.assertEqual(boot.find(b"lsik"), -1)

    def test_reassembled_pak_matches_signature_and_listing_has_no_keystore(self):
        inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
        code, text = audit(APK, CACHE, inventory)
        self.assertEqual(code, 0)
        self.assertIn("res.pak: OK", text)
        self.assertIn("Icon_90.png: OK", text)
        self.assertIn("plaintext_obm_trailer=True", text)
        self.assertIn("keystore.dat: AUSENTE", text)
        self.assertIn("DoubleDragon.exe: OK", text)
        self.assertIn("elf=False", text)
        self.assertIn("não grava instruções", text)
        pak = (CACHE / "res.pak").read_bytes()
        self.assertEqual(len(pak), inventory["reassembly"]["res.pak_size"])
        self.assertTrue(pak_is_plaintext_container(pak))
        names = [row["path"] for row in inventory["cache_files_name_size_only"]]
        self.assertFalse(any(path.endswith("keystore.dat") for path in names))
        self.assertFalse(any(path.endswith("datastore.dat") for path in names))
        exe = (CACHE / "DoubleDragon.exe").read_bytes()
        self.assertNotEqual(exe[:4], b"\x7fELF")
        self.assertEqual(len(exe) % 16, 10)


if __name__ == "__main__":
    unittest.main()
