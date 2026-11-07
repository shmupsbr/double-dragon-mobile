#!/usr/bin/env python3
"""Trace the keystore password derivation and one labeled GCM trial.

The 16-byte mask is taken from the stores in bootstrap.exe at 0x7e576-0x7e582.
kdQueryAttribcv(0x24d) supplies the other XOR operand at runtime. That string
is not in the supplied APK or cache. The fixture below is synthetic and is
not a device id from this install. The trial does not decrypt DoubleDragon.exe.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs
from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_THUMB, Uc
from unicorn.arm_const import (
    UC_ARM_REG_LR,
    UC_ARM_REG_PC,
    UC_ARM_REG_R0,
    UC_ARM_REG_R1,
    UC_ARM_REG_R2,
    UC_ARM_REG_SP,
)

BOOT = Path("/tmp/dd-work/assets/bootstrap.exe")
EXE = Path("/workspace/investigation/cache1327/DoubleDragon.exe")
AUDIO = Path("/workspace/investigation/cache1327/libaudio.so")

# Final stores into the 16-byte buffer, in execution order (dest offset, pool).
MASK_STORES = (
    (8, 0x7E576),
    (4, 0x7E57A),
    (12, 0x7E57E),
    (0, 0x7E582),
)

FN = 0x7E500
STOP = 0x90F98  # real function, not a PLT stub: keystore consumer
STRING_SIZED = 0x56FD0  # std::string(const char*, size, alloc)
STRING_CSTR = 0x56D4C  # std::string(const char*, alloc)
LEAK = 0x56990
GET_ERROR = 0x57FE4  # kdGetError
QUERY = 0x577C0  # kdQueryAttribcv


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def pool_word(boot: bytes, insn: int) -> int:
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if False else CS_ARCH_ARM)
    # Thumb ldr literal: imm is in the instruction text via capstone thumb.
    from capstone import CS_MODE_THUMB

    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    ins = next(md.disasm(boot[insn : insn + 4], insn))
    imm = int(ins.op_str.split("#")[-1].rstrip("]"), 0)
    pc = (insn + 4) & ~3
    return u32(boot, pc + imm)


def mask_bytes(boot: bytes) -> bytes:
    out = bytearray(16)
    for dest, insn in MASK_STORES:
        out[dest : dest + 4] = pool_word(boot, insn).to_bytes(4, "little")
    return bytes(out)


def plt_name(boot: bytes, thumb_addr: int) -> str:
    if boot[thumb_addr : thumb_addr + 2] != b"\x78\x47":
        return ""
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    arm = thumb_addr + 4
    ins = list(md.disasm(boot[arm : arm + 12], arm))
    if len(ins) < 3:
        return ""

    def imm(op: str) -> int:
        return int(op.split("#")[-1].split("]")[0], 0)

    slot = (arm + 8) + imm(ins[0].op_str) + imm(ins[1].op_str) + imm(ins[2].op_str)
    # R_ARM_JUMP_SLOT in .rel.plt
    rel_off, rel_sz = 0x55158, 0x1270
    shoff = 0x1DB0C0
    dynsym = struct.unpack_from("<IIIIIIIIII", boot, shoff + 2 * 40)[4]
    strtab = struct.unpack_from("<IIIIIIIIII", boot, shoff + 3 * 40)[4]
    for i in range(rel_sz // 8):
        off = u32(boot, rel_off + i * 8)
        info = u32(boot, rel_off + i * 8 + 4)
        if off != slot:
            continue
        name_at = u32(boot, dynsym + (info >> 8) * 16)
        end = boot.index(b"\x00", strtab + name_at)
        return boot[strtab + name_at : end].decode()
    return ""


class Heap:
    def __init__(self, uc: Uc, base: int = 0x40000000) -> None:
        self.uc = uc
        self.ptr = base + 0x100

    def alloc(self, n: int) -> int:
        n = (n + 15) & ~15
        addr = self.ptr
        self.ptr += n
        return addr


def make_string(uc: Uc, heap: Heap, text: bytes) -> int:
    """libstdc++ COW string: object is a pointer to chars; Rep is 12 bytes before."""
    rep = heap.alloc(12 + len(text) + 1)
    data = rep + 12
    uc.mem_write(rep, struct.pack("<III", len(text), max(len(text), 1), 0xFFFFFFFF))
    uc.mem_write(data, text + b"\x00")
    return data


def read_cstr(uc: Uc, addr: int, limit: int = 128) -> bytes:
    raw = uc.mem_read(addr, limit)
    out = bytearray()
    for b in raw:
        if b == 0:
            break
        out.append(b)
    return bytes(out)


def emulate_xor(boot: bytes, device: bytes) -> dict:
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.mem_map(0, 0x200000)
    uc.mem_write(0, boot)
    uc.mem_map(0x30000000, 0x200000)
    uc.mem_map(0x40000000, 0x200000)
    heap = Heap(uc)
    device_addr = heap.alloc(len(device) + 1)
    uc.mem_write(device_addr, device + b"\x00")
    sp = 0x30010000
    uc.reg_write(UC_ARM_REG_SP, sp)
    uc.reg_write(UC_ARM_REG_R0, 0x30020000)
    stopped = {"hit": False, "r0": 0, "r1": 0, "r2": 0, "seen": [], "n": 0}

    def hook(_uc, address, _size, _user):
        pc = address
        stopped["n"] += 1
        stopped["seen"].append(pc)
        if len(stopped["seen"]) > 40:
            stopped["seen"].pop(0)
        if pc == STOP:
            stopped["hit"] = True
            stopped["r0"] = _uc.reg_read(UC_ARM_REG_R0)
            stopped["r1"] = _uc.reg_read(UC_ARM_REG_R1)
            stopped["r2"] = _uc.reg_read(UC_ARM_REG_R2)
            _uc.emu_stop()
            return
        lr = _uc.reg_read(UC_ARM_REG_LR)
        if pc == GET_ERROR:
            _uc.reg_write(UC_ARM_REG_R0, 0)
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if pc == QUERY:
            # Only the attribute id loaded immediately before this call is defined.
            attrib = _uc.reg_read(UC_ARM_REG_R0)
            if attrib != 0x24D:
                raise RuntimeError(f"unexpected kdQueryAttribcv attribute {attrib:#x}")
            _uc.reg_write(UC_ARM_REG_R0, device_addr)
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if pc == LEAK:
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if 0x563E0 <= pc < 0x58848 and pc not in (STRING_SIZED, STRING_CSTR, LEAK, GET_ERROR, QUERY):
            # Unmodelled libstdc++ / OpenKODE stub. Returning 0 keeps the
            # in-binary control flow (several sites branch back to the
            # keystore open after a dispose call).
            _uc.reg_write(UC_ARM_REG_R0, 0)
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return
        if pc in (STRING_SIZED, STRING_CSTR):
            this = _uc.reg_read(UC_ARM_REG_R0)
            src = _uc.reg_read(UC_ARM_REG_R1)
            if pc == STRING_SIZED:
                n = _uc.reg_read(UC_ARM_REG_R2)
                blob = bytes(_uc.mem_read(src, n))
            else:
                blob = read_cstr(_uc, src, 256)
            data = make_string(_uc, heap, blob)
            _uc.mem_write(this, struct.pack("<I", data))
            _uc.reg_write(UC_ARM_REG_PC, lr)
            return

    uc.hook_add(UC_HOOK_CODE, hook)
    try:
        uc.emu_start(FN | 1, 0, timeout=2_000_000, count=200_000)
    except Exception as exc:
        pc = uc.reg_read(UC_ARM_REG_PC)
        raise RuntimeError(f"{exc} pc={pc:#x} seen={stopped['seen']}") from exc
    if not stopped["hit"]:
        pc = uc.reg_read(UC_ARM_REG_PC)
        raise RuntimeError(f"did not reach keystore consumer 0x90f98 pc={pc:#x} seen={stopped['seen']}")
    # r2 is the std::string object (address of the pointer), per the call site.
    data_ptr = u32(bytes(uc.mem_read(stopped["r2"], 4)), 0)
    length = u32(bytes(uc.mem_read(data_ptr - 12, 4)), 0)
    derived = bytes(uc.mem_read(data_ptr, length))
    path_ptr = u32(bytes(uc.mem_read(stopped["r1"], 4)), 0)
    path = read_cstr(uc, path_ptr).decode()
    return {"derived": derived, "path": path, "length": length}


def iv_parameter_name(boot: bytes) -> bytes:
    """Recompute the NameValuePairs name built at 0xa7eee for SetKey."""
    # 0xa7ee8 ldr r5,[pc,#imm]; 0xa7ef2 add r5, pc; 0xa7eee ldr r1,[pc,#imm]; 0xa7f04 add r1, r5
    pc = (0xA7EE8 + 4) & ~3
    r5 = (0xA7EF2 + 4 + struct.unpack_from("<i", boot, pc + pool_imm(boot, 0xA7EE8))[0]) & 0xFFFFFFFF
    pc = (0xA7EEE + 4) & ~3
    delta = struct.unpack_from("<i", boot, pc + pool_imm(boot, 0xA7EEE))[0]
    addr = (r5 + delta) & 0xFFFFFFFF
    return boot[addr : addr + 2]


def pool_imm(boot: bytes, insn: int) -> int:
    from capstone import CS_MODE_THUMB

    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    ins = next(md.disasm(boot[insn : insn + 4], insn))
    return int(ins.op_str.split("#")[-1].rstrip("]"), 0)


def gcm_mask_authenticates(exe: bytes, mask: bytes) -> bool:
    """One layout matching istream::read(16) at offset 0 and a 16-byte tag suffix.
    This is a single elimination trial, not a search."""
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM

    if len(mask) != 16 or len(exe) < 48:
        return False
    iv, body, tag = exe[:16], exe[16:-16], exe[-16:]
    try:
        AESGCM(mask).decrypt(iv, body + tag, None)
        return True
    except Exception:
        return False


def wrapped_xor(mask: bytes, device: bytes) -> bytes:
    if not device:
        return mask
    return bytes(mask[i] ^ device[i % len(device)] for i in range(len(mask)))


def main() -> int:
    boot = BOOT.read_bytes()
    exe = EXE.read_bytes()
    mask = mask_bytes(boot)
    print("module_stream", plt_name(boot, 0x56E54))
    print("module_seek", plt_name(boot, 0x56534), "position_words_zeroed_at 0x812bc")
    print("module_read", plt_name(boot, 0x580E4), "length 16")
    print("mask_hex", mask.hex())
    print("query_plt", plt_name(boot, QUERY), "attribute 0x24d returns a C string")
    empty = emulate_xor(boot, b"")
    print("empty_attribute_derived_hex", empty["derived"].hex(), "path", empty["path"], "len", empty["length"])
    fixture = b"FIXTURE-DEVICE!"  # synthetic, not an id from this install
    mixed = emulate_xor(boot, fixture)
    expect = wrapped_xor(mask, fixture)
    print("fixture_derived_hex", mixed["derived"].hex())
    print("fixture_matches_wrapped_xor", mixed["derived"] == expect, "path", mixed["path"])
    audio = AUDIO.read_bytes()
    authed = gcm_mask_authenticates(exe, mask)
    authed_audio = gcm_mask_authenticates(audio, mask)
    # keystore function resizes the copied password to 32 with fill 0, then reads 16.
    print("keystore_resize_imm", boot[0x8F30C : 0x8F30E].hex(), "expect 2021 movs r1,#0x20")
    print("keystore_read_imm", boot[0x8F31C : 0x8F31E].hex(), "expect 1022 movs r2,#0x10")
    print("module_read_imm", boot[0x86928 : 0x8692A].hex(), "expect 1022 movs r2,#0x10")
    print("iv_parameter_name", iv_parameter_name(boot))
    print("gcm_exe_mask16_iv0_tag_end", authed)
    print("gcm_audio_mask16_iv0_tag_end", authed_audio)
    print("exe_head", exe[:4].hex(), "audio_head", audio[:4].hex())
    print("original_module_recovered", False)
    ok = (
        mixed["derived"] == expect
        and not authed
        and not authed_audio
        and empty["path"] == "/data/keystore.dat"
        and iv_parameter_name(boot) == b"IV"
        and boot[0x8F30C : 0x8F30E] == b"\x20\x21"
        and boot[0x86928 : 0x8692A] == b"\x10\x22"
    )
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print("PROBE_FAIL", type(exc).__name__, exc, file=sys.stderr)
        raise
