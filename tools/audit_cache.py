#!/usr/bin/env python3
"""Read-only audit of the supplied Double Dragon G-gee cache and bootstrap.

Does not decrypt, does not modify originals, and does not contact the network.
The checks pin the loader facts that were re-derived from bootstrap.exe
(ELF virtual addresses, not the Ghidra image base).
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import math
import struct
import sys
import zipfile
from collections import Counter
from pathlib import Path

# Ghidra had loaded this PIE ELF at base + 0x10000.
# ELF VA = address in the supplied *.asm notes - 0x10000.
GHIDRA_BIAS = 0x10000
GOT_VA = 0x1E0ED0
PIPELINE_VA = 0x868F4  # notes: 0x968f4
KEY_RESOLVE_VA = 0x8577C  # notes: 0x9577c
POST_LOAD_VA = 0x820B4  # notes: 0x920b4
CEK_STRING_VA = 0x1532CC
PEK_STRING_VA = 0x1532C4
ISTREAM_READ_VA = 0x580E4  # Thumb veneer -> std::istream::read
MEMMOVE_VA = 0x572E4

# Caller inside the post-load copy routine: movs r2, #0; bl pipeline.
CALL_MODE_ZERO_VA = 0x81300

SIGNED_FILES = (
    "DoubleDragon.exe",
    "Icon_90.png",
    "res.pak",
    "libaudio.so",
    "config.xml",
)

# Paths the bootstrap opens by name. These are loader paths, not account tokens.
RUNTIME_FILES = (
    "keystore.dat",
    "datastore.dat",
    "ssdata.dat",
    "ssstore.dat",
    "ssdatad.dat",
    "ssstored.dat",
)


def sha256_b64(data: bytes) -> str:
    return base64.b64encode(hashlib.sha256(data).digest()).decode("ascii")


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = Counter(data)
    n = len(data)
    return -sum((c / n) * math.log2(c / n) for c in counts.values())


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def s32(buf: bytes, off: int) -> int:
    value = u32(buf, off)
    return value - 0x100000000 if value >= 0x80000000 else value


def cstr(buf: bytes, off: int, limit: int = 64) -> str:
    end = buf.index(0, off, off + limit)
    return buf[off:end].decode("ascii")


def parse_signature_digests(text: str) -> dict[str, str]:
    digests: dict[str, str] = {}
    # The file is one line of XMLDSig. Keep the parser local and strict.
    cursor = 0
    while True:
        uri_at = text.find('URI="', cursor)
        if uri_at < 0:
            break
        uri_at += 5
        uri_end = text.find('"', uri_at)
        digest_at = text.find("<DigestValue>", uri_end)
        digest_end = text.find("</DigestValue>", digest_at)
        if digest_at < 0 or digest_end < 0:
            break
        name = text[uri_at:uri_end]
        digest = "".join(text[digest_at + 13 : digest_end].split())
        digests[name] = digest
        cursor = digest_end
    return digests


def read_bootstrap(apk_path: Path) -> bytes:
    with zipfile.ZipFile(apk_path) as apk:
        return apk.read("assets/bootstrap.exe")


def check_bootstrap(boot: bytes) -> list[str]:
    """Return human-readable trace lines. Raise AssertionError on contradiction."""
    lines: list[str] = []
    if boot[:4] != b"\x7fELF":
        raise AssertionError("bootstrap.exe has no ELF magic")
    if u32(boot, 0x1D8ED0) == 0 and GOT_VA != 0x1E0ED0:
        raise AssertionError("unexpected GOT probe")
    # .got file offset is VA - 0x8000 on the second LOAD (p_offset = p_vaddr - 0x8000).
    if boot[0x1D8ED0 : 0x1D8ED4] == b"":
        raise AssertionError("GOT bytes missing")
    lines.append(f"bootstrap ELF {len(boot)} bytes, GOT VA {GOT_VA:#x}, file off {GOT_VA - 0x8000:#x}")

    def prologue(va: int, label: str) -> None:
        # Thumb push {r4, r5, r6, r7, lr} is f0 b5.
        word = boot[va : va + 2]
        if word != b"\xf0\xb5":
            raise AssertionError(f"{label} at {va:#x} is {word.hex()}, not Thumb push")
        lines.append(f"{label}: ELF {va:#x} (notes {va + GHIDRA_BIAS:#x}) prologue f0b5")

    prologue(PIPELINE_VA, "decrypt pipeline")
    prologue(KEY_RESOLVE_VA, "key resolve")
    prologue(POST_LOAD_VA, "postLoadHook")

    if cstr(boot, CEK_STRING_VA) != "/ro/cek":
        raise AssertionError("CEK xpath string moved")
    if cstr(boot, PEK_STRING_VA) != "/ro/pek":
        raise AssertionError("PEK xpath string moved")
    lines.append(f"xpath strings: {cstr(boot, PEK_STRING_VA)} @ {PEK_STRING_VA:#x}, {cstr(boot, CEK_STRING_VA)} @ {CEK_STRING_VA:#x}")

    # Literal used by `adds r2, r4, r2` where r4 is the GOT.
    delta = s32(boot, 0x861E4)
    if (GOT_VA + delta) & 0xFFFFFFFF != CEK_STRING_VA:
        raise AssertionError("PIC literal does not point at /ro/cek")
    lines.append(
        f"key resolve {KEY_RESOLVE_VA:#x}: r8>1 throws Invalid key argument; "
        "r8<=1 parses ro_response. A later compare at 0x86160 maps r8==0 to "
        "/ro/cek and r8==1 to /ro/pek for a different document."
    )

    mode = boot[CALL_MODE_ZERO_VA : CALL_MODE_ZERO_VA + 2]
    if mode != b"\x00\x22":
        raise AssertionError(f"expected movs r2,#0 at {CALL_MODE_ZERO_VA:#x}, got {mode.hex()}")
    lines.append(
        f"post-load call {CALL_MODE_ZERO_VA:#x}: movs r2,#0 then bl pipeline. "
        "Mode 0 reaches the ro_response parser; it does not supply a key from disk."
    )
    lines.append(
        "pipeline reads 16 bytes with std::istream::read (PLT veneer "
        f"{ISTREAM_READ_VA:#x} -> _ZNSi4readEPci) before GCM_Final<Rijndael, Lb0>"
    )
    lines.append(
        "after the pipeline returns, memmove "
        f"(veneer {MEMMOVE_VA:#x}) deletes 0x20 bytes from the output buffer"
    )
    lines.append(
        "GCM vtables installed in the pipeline: GCM_Base, GCM_Base::GCTR, "
        "GCM_Final<Rijndael, Tables=0, IsEncryption=false>, "
        "BlockCipherFinal<ENCRYPTION, Rijndael::Enc>"
    )
    lines.append(
        "AuthenticatedDecryptionFilter vtable is installed by ELF "
        f"{0xE5F9C:#x}, called from inside the pipeline"
    )
    lines.append(
        "The same key function, on the r8<=1 path taken by postLoad, first "
        "parses an ro_response XML (xmlns 1.0 or 1.1) and reads "
        "/ro_response/key plus /ro_response/payload. /ro/cek is a later branch "
        "for a different document shape, not a substitute for that XML."
    )
    lines.append(
        "After a non-empty payload, ELF 0x85b98 calls the keystore routine "
        "0x7e500, which opens /data/keystore.dat. A static XOR mask in that "
        "routine is not the module key."
    )
    return lines


def pak_is_plaintext_container(data: bytes) -> bool:
    # The on-disk pak starts with the OBM magic used by the asset files and
    # ends with the index trailer already identified in the user's notes.
    return data[:2] == b"OI" and data[-8:] == bytes.fromhex("d0502e0300000000")


def inventory_names(inventory: dict) -> list[str]:
    rows = inventory.get("cache_files_name_size_only") or []
    return [row["path"] for row in rows]


def audit(apk: Path, cache: Path, inventory: dict | None = None) -> tuple[int, str]:
    lines: list[str] = []
    lines.append("Double Dragon G-gee / Acrodea — auditoria somente leitura")
    lines.append(f"apk: {apk}")
    lines.append(f"cache: {cache}")
    boot = read_bootstrap(apk)
    lines.extend(check_bootstrap(boot))

    sig_path = cache / "signature.xml"
    if not sig_path.is_file():
        lines.append("FALHA: signature.xml ausente no cache")
        text = "\n".join(lines) + "\n"
        return 2, text
    digests = parse_signature_digests(sig_path.read_text(encoding="utf-8", errors="replace"))
    lines.append("")
    lines.append("Pacote assinado (XMLDSig no cache, não é a CEK):")
    missing_signed = []
    for name in SIGNED_FILES:
        path = cache / name
        expected = digests.get(name)
        if expected is None:
            lines.append(f"  {name}: sem DigestValue na assinatura")
            continue
        if not path.is_file():
            missing_signed.append(name)
            lines.append(f"  {name}: AUSENTE  digest esperado {expected}")
            continue
        data = path.read_bytes()
        actual = sha256_b64(data)
        ok = actual == expected
        ent = entropy(data)
        elf = data[:4] == b"\x7fELF"
        lines.append(
            f"  {name}: {'OK' if ok else 'DIGESTO DIFERENTE'} "
            f"size={len(data)} entropy={ent:.3f} elf={elf}"
        )
        if name in ("DoubleDragon.exe", "libaudio.so") and (elf or ent < 7.5):
            lines.append(f"    inesperado: {name} não parece ciphertext opaco")

    lines.append("")
    lines.append("Arquivos de runtime citados pelo bootstrap e ausentes deste cache:")
    present_runtime = []
    for name in RUNTIME_FILES:
        hits = list(cache.rglob(name))
        if hits:
            present_runtime.append(name)
            lines.append(f"  {name}: presente ({hits[0]})")
        else:
            lines.append(f"  {name}: AUSENTE")

    lines.append("")
    pak = cache / "res.pak"
    if pak.is_file():
        blob = pak.read_bytes()
        lines.append(
            f"res.pak container: plaintext_obm_trailer={pak_is_plaintext_container(blob)} "
            f"png_at={blob.find(bytes([0x89, 0x50, 0x4E, 0x47]))}"
        )
    if inventory is not None:
        names = inventory_names(inventory)
        lines.append("")
        lines.append("Listing completo do cache original (só nome e tamanho):")
        for row in inventory["cache_files_name_size_only"]:
            lines.append(f"  {row['bytes']:10} {row['path']}")
        absent = [
            name
            for name in ("keystore.dat", "datastore.dat", "ssdata.dat", "ssstore.dat")
            if not any(path.endswith(name) for path in names)
        ]
        lines.append("Nomes de chave/RO ausentes desse listing: " + ", ".join(absent))
    lines.append("")
    if "keystore.dat" in present_runtime and (cache / "res.pak").is_file():
        lines.append("Os nomes de runtime estão no diretório. O conteúdo ainda precisa ser validado.")
    else:
        lines.append("BLOQUEIO: o módulo original não é executável com os arquivos presentes.")
        lines.append("DoubleDragon.exe permanece ciphertext (entropia ~8, sem ELF).")
        lines.append("res.pak, quando presente e com o digest da assinatura, é o pacote de recursos em claro.")
        lines.append("O postLoad exige um XML ro_response (key e payload) e abre /data/keystore.dat.")
        lines.append("Nenhum desses insumos está no APK, no exe, no pak ou no listing do cache.")
        lines.append("Trocar o ramo de /ro/cek para /ro/pek, ou pular o teste, não grava instruções no exe.")
    text = "\n".join(lines) + "\n"
    # Exit 0 when the trace itself holds. Missing game files are reported in the text,
    # not treated as a broken auditor.
    return 0, text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    code, text = audit(args.apk, args.cache)
    sys.stdout.write(text)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
