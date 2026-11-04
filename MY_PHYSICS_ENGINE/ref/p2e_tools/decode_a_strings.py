#!/usr/bin/env python3
"""Decode the obfuscated string table of one game class `a` (the one whose static byte[] sits at +0x4FE8).
Static ctor (RVA 0x5DF463C): byte[20451] = InitializeArray(<metadata blob @0x94D798>), then b[i] = b[i] ^ (i & 0xFF) ^ 0xAA.
Accessor a.xx() = UTF8.GetString(b, offset, count); e.g. dx() = (0x65, 0xEFF, 4) = "Edge", uj() = (0x211, 0x4D8C, 5) = "Table".
usage: decode_a_strings.py global-metadata.dat [offset count]"""
import sys
d = open(sys.argv[1], 'rb').read()
raw = d[0x94D798:0x94D798 + 20451]
b = bytes(x ^ (i & 0xFF) ^ 0xAA for i, x in enumerate(raw))
if len(sys.argv) >= 4:
    o, n = int(sys.argv[2], 0), int(sys.argv[3], 0); print(b[o:o + n].decode('utf8', 'replace'))
else:
    sys.stdout.buffer.write(b)
