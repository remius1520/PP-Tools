#!/usr/bin/env python3
"""run_fn.py — Run one function from libunity.so under Unicorn (AArch64).

Usage:
    python3 tools/emu/run_fn.py <libunity.so> <func_va> [x0=...] [x1=...] ... [s0=...] ...

Loads every PT_LOAD segment at its native VA, applies R_AARCH64_RELATIVE
relocations, maps a stack, sets PC=func_va and the given registers, then runs
until the function returns (PC == lr-style return) or a fault.

On unmapped memory / unsupported instruction STOP and report the address —
never guess. Prints final register state and the bytes at [x0]+0x94/+0x98 for
the sleep setters so callers can verify the store happened.
"""

import sys
import struct

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_LITTLE_ENDIAN, UcError, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_FETCH_INVALID, UC_HOOK_MEM_READ_INVALID, UC_HOOK_MEM_WRITE_INVALID, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_LR,
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X6, UC_ARM64_REG_X7,
    UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2, UC_ARM64_REG_S3,
    UC_ARM64_REG_S4, UC_ARM64_REG_S5, UC_ARM64_REG_S6, UC_ARM64_REG_S7,
)

X_REGS = [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
          UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X6, UC_ARM64_REG_X7]
S_REGS = [UC_ARM64_REG_S0, UC_ARM64_REG_S1, UC_ARM64_REG_S2, UC_ARM64_REG_S3,
          UC_ARM64_REG_S4, UC_ARM64_REG_S5, UC_ARM64_REG_S6, UC_ARM64_REG_S7]

STACK_ADDR = 0x7fff0000
STACK_SIZE = 0x100000
OBJ_PAGE_ADDR = 0x90000000   # scratch page for objects passed to functions
OBJ_PAGE_SIZE = 0x1000
MAX_INSN = 200000


def parse_args(argv):
    """Return (elf_path, func_va, x_vals, s_vals) from argv."""
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        sys.exit(2)
    elf_path = argv[1]
    try:
        func_va = int(argv[2], 0)
    except ValueError:
        sys.stderr.write(f"bad func_va: {argv[2]!r}\n")
        sys.exit(2)
    x_vals = {}
    s_vals = {}
    X_REG_NAMES = {r: n for n, r in enumerate(["x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"])}
    for a in argv[3:]:
        if "=" not in a:
            sys.stderr.write(f"bad arg (expect NAME=VALUE): {a!r}\n")
            sys.exit(2)
        name, val = a.split("=", 1)
        name = name.strip()
        if name in X_REG_NAMES:
            x_vals[name] = int(val, 0)
        elif name.startswith("s") and name[1:].isdigit():
            s_vals[name] = float(val)
        else:
            sys.stderr.write(f"unknown register: {name!r}\n")
            sys.exit(2)
    return elf_path, func_va, x_vals, s_vals


class Emu:
    def __init__(self, elf_path):
        self.elf = ELFFile(open(elf_path, "rb"))
        self._fh = open(elf_path, "rb")
        self.uc = Uc(UC_ARCH_ARM64, UC_MODE_LITTLE_ENDIAN)
        self.insn_count = 0
        self.stop_addr = None
        self.stop_reason = None

        self._load_segments()
        self._apply_relative_relocs()
        self._map_stack()
        self._install_hooks()

    # ---------------------------------------------------------------- load

    def _load_segments(self):
        for seg in self.elf.iter_segments():
            if seg["p_type"] != "PT_LOAD":
                continue
            va = seg["p_vaddr"]
            off = seg["p_offset"]
            fsize = seg["p_filesz"]
            msize = seg["p_memsz"]
            perm = 0
            if seg["p_flags"] & 0x1: perm |= UC_PROT_READ
            if seg["p_flags"] & 0x2: perm |= UC_PROT_WRITE
            if seg["p_flags"] & 0x4: perm |= UC_PROT_EXEC

            # Align mapping to page for mprotect-friendly perms; Unicorn needs
            # the region we map to cover the whole [va, va+msize).
            # Map file-backed part + zero-fill to msize.
            map_addr = va & ~0xfff
            map_size = ((va + msize + 0xfff) & ~0xfff) - map_addr
            self._fh.seek(off)
            data = self._fh.read(fsize) if fsize else b""
            self.uc.mem_map(map_addr, map_size, perm)
            if data:
                # data starts at (va - map_addr) within the mapped region
                self.uc.mem_write(va, data)
            # Zero-fill bss tail: [va+fsize, va+msize)
            if msize > fsize:
                self.uc.mem_write(va + fsize, b"\x00" * (msize - fsize))
            print(f"[emu] mapped PT_LOAD va={va:#x} off={off:#x} fsize={fsize:#x} "
                  f"msize={msize:#x} perm={perm:#o}")

    def _apply_relative_relocs(self):
        """Apply R_AARCH64_RELATIVE relocations so absolute addresses resolve."""
        applied = 0
        for sec in self.elf.iter_sections():
            if not isinstance(sec, RelocationSection):
                continue
            for reloc in sec.iter_relocations():
                # st_info >> 32 on AArch64 gives type; use 'type' field if present
                rtype = getattr(reloc, "type", None)
                if rtype is None:
                    # fallback: interpret r_info
                    r_info = reloc["r_info"]
                    rtype = r_info >> 32
                if rtype != 1024:  # R_AARCH64_RELATIVE
                    continue
                r_offset = reloc["r_offset"]
                r_addend = reloc["r_addend"]
                abs_addr = r_addend  # load_bias == 0 (mapping at native VA)
                cur = self.uc.mem_read_u64(r_offset)
                self.uc.mem_write_u64(r_offset, abs_addr)
                applied += 1
                if applied <= 5:
                    print(f"[emu] RELATIVE reloc {r_offset:#x} <- {abs_addr:#x}")
        print(f"[emu] applied {applied} R_AARCH64_RELATIVE relocations")

    def _map_stack(self):
        self.uc.mem_map(STACK_ADDR, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        sp = STACK_ADDR + STACK_SIZE - 0x10
        self.uc.reg_write(UC_ARM64_REG_SP, sp)
        self.uc.mem_map(OBJ_PAGE_ADDR, OBJ_PAGE_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        print(f"[emu] stack at {STACK_ADDR:#x}-{STACK_ADDR+STACK_SIZE:#x} sp={sp:#x}")
        print(f"[emu] object page at {OBJ_PAGE_ADDR:#x}-{OBJ_PAGE_ADDR+OBJ_PAGE_SIZE:#x}")

    # ---------------------------------------------------------------- hooks

    def _install_hooks(self):
        def mem_invalid(uc, access, address, size, value, user_data):
            self.stop_addr = address
            self.stop_reason = (
                "write" if access == Uc.WRITE_MEM_INVALID else "read"
            ) + f" invalid memory at {address:#x} size={size}"
            return False  # stop emulation

        def insn_invalid(uc, user_data):
            self.stop_addr = uc.reg_read(UC_ARM64_REG_PC)
            self.stop_reason = f"unsupported instruction at {self.stop_addr:#x}"
            return False

        def code_count(uc, address, size, user_data):
            self.insn_count += 1
            if self.insn_count > MAX_INSN:
                self.stop_addr = address
                self.stop_reason = f"instruction cap {MAX_INSN} exceeded at {address:#x}"
                return False
            return True

        self.uc.hook_add(UC_HOOK_MEM_INVALID, mem_invalid)
        self.uc.hook_add(UC_HOOK_MEM_FETCH_INVALID | UC_HOOK_MEM_READ_INVALID |
                         UC_HOOK_MEM_WRITE_INVALID, mem_invalid)
        self.uc.hook_add(UC_HOOK_CODE, code_count)

    # ---------------------------------------------------------------- run

    def run(self, func_va, x_vals, s_vals):
        lr = 0xdeadbeefdeadbeef  # sentinel so we can detect "returned"
        self.uc.reg_write(UC_ARM64_REG_PC, func_va)
        self.uc.reg_write(UC_ARM64_REG_LR, lr)
        for name, val in x_vals.items():
            reg = {"x0": UC_ARM64_REG_X0, "x1": UC_ARM64_REG_X1,
                   "x2": UC_ARM64_REG_X2, "x3": UC_ARM64_REG_X3,
                   "x4": UC_ARM64_REG_X4, "x5": UC_ARM64_REG_X5,
                   "x6": UC_ARM64_REG_X6, "x7": UC_ARM64_REG_X7}[name]
            self.uc.reg_write(reg, val)
        for name, val in s_vals.items():
            idx = int(name[1:])
            bits = struct.unpack("<I", struct.pack("<f", float(val)))[0]
            self.uc.reg_write(S_REGS[idx], bits)

        try:
            self.uc.emu_start(func_va, 0x10000000000000000)
        except UcError as e:
            pc = self.uc.reg_read(UC_ARM64_REG_PC)
            self.stop_addr = pc
            self.stop_reason = f"UcError: {e} at PC={pc:#x}"

        return self._report(lr)

    def _report(self, lr_sentinel):
        out = []
        out.append("=== Unicorn run result ===")
        if self.stop_reason:
            out.append(f"STOPPED: {self.stop_reason} (PC={self.stop_addr:#x})")
            out.append("INSTRUCTIONS EXECUTED: %d" % self.insn_count)
        else:
            out.append("COMPLETED: function returned (no fault)")
            out.append(f"INSTRUCTIONS EXECUTED: {self.insn_count}")

        regs = {}
        for i, r in enumerate(X_REGS):
            regs[f"x{i}"] = self.uc.reg_read(r)
        for i, r in enumerate(S_REGS):
            regs[f"s{i}"] = struct.unpack("<f", struct.pack("<I",
                             self.uc.reg_read(r) & 0xffffffff))[0]
        regs["pc"] = self.uc.reg_read(UC_ARM64_REG_PC)
        regs["sp"] = self.uc.reg_read(UC_ARM64_REG_SP)
        regs["lr"] = self.uc.reg_read(UC_ARM64_REG_LR)
        out.append("--- registers ---")
        def rkey(t):
            if t == "pc": return (0, 0)
            if t == "sp": return (1, 0)
            if t == "lr": return (2, 0)
            if t[0] in "xs" and t[1:].isdigit(): return (3, int(t[1:]))
            return (4, t)
        for k in sorted(regs, key=rkey):
            v = regs[k]
            if isinstance(v, float):
                out.append(f"  {k:4s} = {v!r}  (bits {struct.unpack('<I', struct.pack('<f', v))[0]:#010x})")
            else:
                out.append(f"  {k:4s} = {v:#x}")

        # For the sleep setters, dump the object at [x0] so the store is visible.
        x0 = regs.get("x0", 0)
        if x0:
            try:
                obj = self.uc.mem_read(x0, 0x9c)
                out.append(f"--- object at x0={x0:#x} (0x9c bytes) ---")
                out.append("  +0x94 (wakeCounter/sleepThreshold?) = "
                           f"{struct.unpack('<f', obj[0x94:0x98])[0]!r}")
                out.append("  +0x98 = "
                           f"{struct.unpack('<f', obj[0x98:0x9c])[0]!r}")
                out.append("  +0x00 (vtable load) = "
                           f"{struct.unpack('<Q', obj[0:8])[0]:#x}")
            except UcError as e:
                out.append(f"--- could not read object at x0={x0:#x}: {e} ---")

        return "\n".join(out)


def main():
    elf_path, func_va, x_vals, s_vals = parse_args(sys.argv)
    emu = Emu(elf_path)
    print(emu.run(func_va, x_vals, s_vals))


if __name__ == "__main__":
    main()