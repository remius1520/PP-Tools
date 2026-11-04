import struct, sys
def rd(path):
    d = open(path,'rb').read()
    ms, fs, ver, do = struct.unpack_from('>IIII', d, 0)
    end = d[16]
    p = 20
    if ver >= 22:
        ms, fs, do, _ = struct.unpack_from('>IqqQ', d, 20); p = 48
    e = '>' if end == 1 else '<'
    # unity version string
    q = d.index(b'\0', p); uv = d[p:q].decode(); q += 1
    plat, = struct.unpack_from(e+'I', d, q); q += 4
    tt = d[q]; q += 1
    nt, = struct.unpack_from(e+'i', d, q); q += 4
    types = []
    for _ in range(nt):
        cid, = struct.unpack_from(e+'i', d, q); q += 4
        q += 1  # stripped
        sidx, = struct.unpack_from(e+'h', d, q); q += 2
        if cid == 114 or sidx >= 0 and False: pass
        if cid == 114: q += 16
        elif sidx != -1 and False: pass
        q += 16  # old type hash
        if tt:
            n, nb = struct.unpack_from(e+'II', d, q); q += 8 + n*32 + nb
            nd, = struct.unpack_from(e+'i', d, q); q += 4 + 4*nd
        types.append(cid)
    no, = struct.unpack_from(e+'i', d, q); q += 4
    objs = []
    for _ in range(no):
        q = (q + 3) & ~3
        pid, off, size, ti = struct.unpack_from(e+'qqIi', d, q); q += 24
        objs.append((pid, off + do, size, types[ti]))
    return d, objs, ver, uv, e
if __name__ == '__main__':
    d, objs, ver, uv, e = rd(sys.argv[1]); print(ver, uv, len(objs))
    from collections import Counter
    print(Counter(o[3] for o in objs))
