import struct, sys, os
def lz4_block(src, usize):
    out = bytearray(); i = 0; n = len(src)
    while i < n:
        t = src[i]; i += 1
        l = t >> 4
        if l == 15:
            while True:
                b = src[i]; i += 1; l += b
                if b != 255: break
        out += src[i:i+l]; i += l
        if i >= n: break
        off = src[i] | (src[i+1] << 8); i += 2
        m = t & 15
        if m == 15:
            while True:
                b = src[i]; i += 1; m += b
                if b != 255: break
        m += 4
        s = len(out) - off
        if off >= m: out += out[s:s+m]
        else:
            for k in range(m): out.append(out[s+k])
    return bytes(out)
def cstr(d, p):
    e = d.index(b'\0', p); return d[p:e].decode(), e+1
def load(path):
    d = open(path,'rb').read()
    sig, p = cstr(d, 0)
    ver, = struct.unpack_from('>I', d, p); p += 4
    v1, p = cstr(d, p); v2, p = cstr(d, p)
    size, cbi, ubi, flags = struct.unpack_from('>qIII', d, p); p += 20
    print(sig, ver, v1, v2, size, cbi, ubi, hex(flags))
    if ver >= 7: p = (p + 15) & ~15
    if flags & 0x80: bi = d[len(d)-cbi:]
    else: bi = d[p:p+cbi]; p += cbi
    if flags & 0x200: p = (p + 15) & ~15
    comp = flags & 0x3f
    if comp == 0: bi = bytes(bi)
    elif comp in (2,3): bi = lz4_block(bi, ubi)
    else: raise Exception('comp %d' % comp)
    q = 16
    nb, = struct.unpack_from('>I', bi, q); q += 4
    blocks = []
    for _ in range(nb):
        u, c, f = struct.unpack_from('>IIH', bi, q); q += 10; blocks.append((u,c,f))
    nn, = struct.unpack_from('>I', bi, q); q += 4
    nodes = []
    for _ in range(nn):
        o, s, f = struct.unpack_from('>qqI', bi, q); q += 20
        nm, q = cstr(bi, q); nodes.append((o,s,f,nm))
    data = bytearray()
    for u,c,f in blocks:
        raw = d[p:p+c]; p += c
        k = f & 0x3f
        data += raw if k == 0 else lz4_block(raw, u) if k in (2,3) else (_ for _ in ()).throw(Exception('blk comp %d'%k))
    return nodes, bytes(data)
if __name__ == '__main__':
    nodes, data = load(sys.argv[1]); out = sys.argv[2]; os.makedirs(out, exist_ok=True)
    for o,s,f,nm in nodes:
        print(nm, s, f); open(os.path.join(out, nm.replace('/','_')),'wb').write(data[o:o+s])
