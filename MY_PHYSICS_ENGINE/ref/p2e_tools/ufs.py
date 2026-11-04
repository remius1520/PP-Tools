import struct, lzma, sys, os

def lz4_block(src, usize):
    dst = bytearray()
    i = 0; n = len(src)
    while i < n:
        t = src[i]; i += 1
        ll = t >> 4
        if ll == 15:
            while True:
                b = src[i]; i += 1; ll += b
                if b != 255: break
        dst += src[i:i+ll]; i += ll
        if i >= n: break
        off = src[i] | (src[i+1] << 8); i += 2
        ml = t & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        s = len(dst) - off
        if off >= ml:
            dst += dst[s:s+ml]
        else:
            chunk = dst[s:]
            q, r = divmod(ml, off)
            dst += chunk * q + chunk[:r]
    return bytes(dst)

def decomp(data, usize, mode):
    if mode == 0: return data
    if mode == 1: return lzma.LZMADecompressor().decompress(data)
    if mode in (2, 3): return lz4_block(data, usize)
    raise ValueError(mode)

def read_cstr(f):
    b = bytearray()
    while True:
        c = f.read(1)
        if c == b'\0' or not c: break
        b += c
    return b.decode()

def open_bundle(path):
    f = open(path, 'rb')
    sig = read_cstr(f)
    ver = struct.unpack('>I', f.read(4))[0]
    pv = read_cstr(f); uv = read_cstr(f)
    size, cbi, ubi, flags = struct.unpack('>QIII', f.read(20))
    if ver >= 7:
        pos = f.tell(); f.seek((pos + 15) & ~15)
    if flags & 0x80:
        hdr_end = f.tell()
        f.seek(-cbi, 2)
        info = decomp(f.read(cbi), ubi, flags & 0x3F)
        f.seek(hdr_end)
    else:
        info = decomp(f.read(cbi), ubi, flags & 0x3F)
        if flags & 0x200:
            f.seek((f.tell() + 15) & ~15)
    off = 16
    nb = struct.unpack('>I', info[off:off+4])[0]; off += 4
    blocks = []
    for _ in range(nb):
        u, c, fl = struct.unpack('>IIH', info[off:off+10]); off += 10
        blocks.append((u, c, fl))
    nn = struct.unpack('>I', info[off:off+4])[0]; off += 4
    nodes = []
    for _ in range(nn):
        o, s_, fl = struct.unpack('>QQI', info[off:off+20]); off += 20
        e = info.index(b'\0', off); nm = info[off:e].decode(); off = e + 1
        nodes.append((nm, o, s_, fl))
    ds = f.tell()
    out = bytearray()
    for u, c, fl in blocks:
        out += decomp(f.read(c), u, fl & 0x3F)
    return {'ver': (pv, uv), 'nodes': nodes, 'data': bytes(out)}

if __name__ == '__main__':
    b = open_bundle(sys.argv[1])
    print(b['ver'])
    for n in b['nodes']: print(n)
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        for nm, o, s, fl in b['nodes']:
            open(os.path.join(sys.argv[2], nm.replace('/', '_')), 'wb').write(b['data'][o:o+s])
