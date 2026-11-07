"""Audit the RO parser branches that feed SetKey.

Fixtures here are labeled and only describe function behavior. Nothing in this
module is a recovered content-encryption key, and nothing decrypts the
original module.
"""

from __future__ import annotations

import struct
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_SP,
)

from tools.envelope_probe import Heap, make_string, read_cstr

BOOT = Path("/tmp/dd-work/assets/bootstrap.exe")
EXE = Path("/workspace/investigation/cache1327/DoubleDragon.exe")
AUDIO = Path("/workspace/investigation/cache1327/libaudio.so")

GOT_VA = 0x1E0ED0
GOT_FILE = 0x1D8ED0
DEVICE_FN = 0x85500
DEVICE_STOP = 0x8553C
QUERY = 0x577C0
STRING_CSTR = 0x56D4C
STRLEN = 0x56DCC
ASSIGN = 0x586B8
FALLBACK_DEVICE = b"12345678901234"


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def thumb_at(boot: bytes, addr: int) -> str:
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    ins = next(md.disasm(boot[addr : addr + 4], addr))
    return f"{ins.mnemonic} {ins.op_str}"


def got_word(boot: bytes, delta: int) -> int:
    return u32(boot, GOT_FILE + delta)


def dynsym_covering(boot: bytes, va: int) -> str:
    shoff = 0x1DB0C0
    dynsym = struct.unpack_from("<IIIIIIIIII", boot, shoff + 2 * 40)[4]
    strtab = struct.unpack_from("<IIIIIIIIII", boot, shoff + 3 * 40)[4]
    count = struct.unpack_from("<IIIIIIIIII", boot, shoff + 2 * 40)[5] // 16
    for i in range(count):
        name, val, size, _info, _other, _shndx = struct.unpack_from(
            "<IIIBBH", boot, dynsym + i * 16
        )
        if size and val <= va < val + size:
            end = boot.index(b"\x00", strtab + name)
            return boot[strtab + name : end].decode()
    return ""


def literal_string(boot: bytes, addr: int) -> bytes:
    """String addressed by `ldr rx, [pc, #imm]` / `add rx, GOT` at `addr`."""
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    ins = next(md.disasm(boot[addr : addr + 4], addr))
    if ins.mnemonic != "ldr" or "[pc" not in ins.op_str:
        raise RuntimeError(f"{addr:#x} is not a literal load")
    imm = int(ins.op_str.split("#")[-1].rstrip("]"), 0)
    disp = u32(boot, ((addr + 4) & ~3) + imm)
    target = (GOT_VA + disp) & 0xFFFFFFFF
    end = boot.index(b"\x00", target)
    return boot[target:end]


def device_id_input(boot: bytes, attribute: bytes | None) -> bytes:
    """Run only the prefix of 0x85500.

    `attribute is None` is a null return from kdQueryAttribcv, which is the
    path that keeps the embedded literal. A bytes value is a labeled fixture
    standing in for attribute 0x24d. Execution stops before SHA-256.
    """
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0, 0x200000)
    uc.mem_write(0, boot[:0x1BAB78])
    uc.mem_map(0x30000000, 0x100000)
    uc.mem_map(0x40000000, 0x100000)
    heap = Heap(uc)
    fixture_addr = 0
    if attribute is not None:
        fixture_addr = heap.alloc(len(attribute) + 1)
        uc.mem_write(fixture_addr, attribute + b"\x00")
    uc.reg_write(UC_ARM_REG_SP, 0x30018000)
    seen = {"stop": False, "query": None, "assigned": None}

    def hook(_uc, address, _size, _user):
        lr = _uc.reg_read(UC_ARM_REG_LR)
        if address == QUERY:
            attrib = _uc.reg_read(UC_ARM_REG_R0)
            seen["query"] = attrib
            if attrib != 0x24D:
                raise RuntimeError(f"unexpected attribute {attrib:#x}")
            _uc.reg_write(UC_ARM_REG_R0, fixture_addr)
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if address == STRING_CSTR:
            this = _uc.reg_read(UC_ARM_REG_R0)
            src = _uc.reg_read(UC_ARM_REG_R1)
            data = make_string(_uc, heap, read_cstr(_uc, src, 64))
            _uc.mem_write(this, struct.pack("<I", data))
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if address == STRLEN:
            src = _uc.reg_read(UC_ARM_REG_R0)
            _uc.reg_write(UC_ARM_REG_R0, len(read_cstr(_uc, src, 64)))
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if address == ASSIGN:
            this = _uc.reg_read(UC_ARM_REG_R0)
            src = _uc.reg_read(UC_ARM_REG_R1)
            n = _uc.reg_read(UC_ARM_REG_R2)
            blob = bytes(_uc.mem_read(src, n))
            seen["assigned"] = blob
            data = make_string(_uc, heap, blob)
            _uc.mem_write(this, struct.pack("<I", data))
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if address == DEVICE_STOP:
            sp = _uc.reg_read(UC_ARM_REG_SP)
            ptr = u32(bytes(_uc.mem_read(sp + 0x10C, 4)), 0)
            seen["text"] = read_cstr(_uc, ptr, 64)
            seen["stop"] = True
            _uc.emu_stop()
            return
        if 0x563E0 <= address < 0x58848:
            raise RuntimeError(f"unexpected PLT {address:#x}")

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(DEVICE_FN | 1, 0, timeout=1_000_000, count=20_000)
    if not seen["stop"]:
        raise RuntimeError(f"did not reach {DEVICE_STOP:#x}")
    if seen["query"] != 0x24D:
        raise RuntimeError("attribute 0x24d was not queried")
    return seen["text"]


def private_key_material_absent(boot: bytes, exe: bytes, audio: bytes) -> dict:
    """No PEM and no RSAPrivateKey version integer in the three binaries."""
    needles = (b"-----BEGIN", b"PRIVATE KEY", b"\x02\x01\x00\x02\x81", b"\x02\x01\x00\x02\x82")
    hits = {}
    for name, blob in (("boot", boot), ("exe", exe), ("audio", audio)):
        hits[name] = {n.hex() if not n.isascii() else n.decode(): blob.find(n) for n in needles}
    return hits


def audit_facts(boot: bytes) -> dict:
    oaep = got_word(boot, 0x17F8)
    sha = got_word(boot, 0x1AF4)
    return {
        "fallback_literal": literal_string(boot, 0x85510),
        "null_attribute_keeps_literal": thumb_at(boot, 0x8552A) == "beq #0x8553c",
        "query_attribute_literal": u32(boot, ((0x85522 + 4) & ~3) + 0x214) == 0x24D,
        "sha256_symbol": dynsym_covering(boot, sha),
        "sha256_got": sha,
        "oaep_decryptor_symbol": dynsym_covering(boot, oaep),
        "oaep_got": oaep,
        "payload_iv_length": thumb_at(boot, 0x85E04),
        "payload_skip_iv": (thumb_at(boot, 0x85EA0), thumb_at(boot, 0x85EA2)),
        "module_iv_length": thumb_at(boot, 0x86A2E),
        "module_key_load": thumb_at(boot, 0x86A0A),
        "mode0_site": thumb_at(boot, 0x81300),
        "mode1_site": thumb_at(boot, 0x6AEF4),
        "cek_string": literal_string(boot, 0x86168),
        "pek_string": literal_string(boot, 0x86350),
        "miss_throws": literal_string(boot, 0x7E9A2),
        "keystore_write_failure": literal_string(boot, 0x90840),
        "null_attribute_sets_error": thumb_at(boot, 0x7E7BC),
        "null_attribute_still_opens": thumb_at(boot, 0x7E654),
    }
