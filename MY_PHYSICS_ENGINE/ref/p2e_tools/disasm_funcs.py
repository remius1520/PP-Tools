#!/usr/bin/env python3
"""Turn pp_funcs.txt (written by the payload) into readable ARM64 assembly.
Usage: python3 tools/disasm_funcs.py analysis/pp_funcs.txt [out.txt]
Needs: pip install capstone   (the CI workflow 'Disassemble physics functions' does this for you)
Data words inside a function (literal pools = float constants) are printed as .word with their float value:
those constants (restitution, friction, 0.5 ...) are exactly what we need for the physics."""
import struct, sys

try:
    import capstone
except ImportError:
    capstone = None


def load(path):
    funcs, base = [], None
    for line in open(path, encoding="utf-8", errors="ignore"):
        if line.startswith("# "):
            for tok in line.split():
                if tok.startswith("base="):
                    base = tok[5:]
        elif line.startswith("F "):
            _, name, rva, n, hx = line.split()
            funcs.append((name, int(rva, 16), int(n), bytes.fromhex(hx)))
    return funcs, base


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    funcs, base = load(sys.argv[1])
    out = open(sys.argv[2], "w", encoding="utf-8") if len(sys.argv) > 2 else sys.stdout
    names = {rva: name for name, rva, _, _ in funcs}
    md = None
    if capstone:
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        md.detail = False
    print(f"# physics functions: {len(funcs)}  libil2cpp base={base}  capstone={'yes' if md else 'MISSING (raw words only)'}", file=out)
    for name, rva, n, code in funcs:
        print(f"\n##### {name}  RVA 0x{rva:X}  {n} bytes", file=out)
        for off in range(0, n - n % 4, 4):
            word = code[off:off + 4]
            addr = rva + off
            w = struct.unpack("<I", word)[0]
            ins = list(md.disasm(word, addr)) if md else []
            if ins:
                i = ins[0]
                txt = f"{i.mnemonic} {i.op_str}".strip()
                if i.mnemonic in ("bl", "b") and i.op_str.startswith("#0x"):
                    tgt = int(i.op_str[1:], 16)
                    if tgt in names:
                        txt += f"   ; -> {names[tgt]}"
                print(f"{addr:8X}: {w:08x}  {txt}", file=out)
            else:
                f = struct.unpack("<f", word)[0]
                print(f"{addr:8X}: {w:08x}  .word  ; float={f:.6g}", file=out)


if __name__ == "__main__":
    main()
