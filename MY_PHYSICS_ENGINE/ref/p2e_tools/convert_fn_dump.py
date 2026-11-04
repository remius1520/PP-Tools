#!/usr/bin/env python3
"""Convert a fn_dump file ("== Class.Method rva=0x... len=N" + "000000: <hex>" lines)
into the pp_funcs.txt format that tools/disasm_funcs.py reads ("F name rva n hex").
Usage: python3 tools/convert_fn_dump.py fn_dump.txt analysis/pp_funcs.txt"""
import re, sys

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    base = ""
    funcs, cur = [], None
    for line in open(src, encoding="utf-8", errors="ignore"):
        line = line.rstrip("\n")
        m = re.search(r"base=(0x[0-9a-fA-F]+)", line)
        if line.startswith("#") and m:
            base = m.group(1)
        m = re.match(r"== (\S+) rva=(0x[0-9a-fA-F]+) len=(\d+)", line)
        if m:
            cur = [m.group(1), m.group(2), int(m.group(3)), ""]
            funcs.append(cur)
            continue
        m = re.match(r"[0-9a-fA-F]{6}: ([0-9a-fA-F]+)\s*$", line)
        if m and cur:
            cur[3] += m.group(1)
    with open(dst, "w") as f:
        f.write(f"# converted from {src} base={base}\n")
        for name, rva, n, hx in funcs:
            f.write(f"F {name} {rva} {len(hx)//2} {hx}\n")
    print(f"{len(funcs)} functions -> {dst}")

if __name__ == "__main__":
    main()
