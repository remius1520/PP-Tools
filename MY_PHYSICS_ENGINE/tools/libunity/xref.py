import struct,sys
d=open('/home/claude/ext/lib/arm64-v8a/libunity.so','rb').read()
TS=0x5438e0; TO=0x53f8e0; TSZ=0x10d6b80
def va2off(va):
    if va<0x53f8dc: return va
    if TS<=va<TS+TSZ: return va-TS+TO
    raise Exception
def xrefs(targets):
    tg=set(targets); res={}
    n=TSZ//4
    words=struct.unpack('<%dI'%n, d[TO:TO+n*4])
    for i,w in enumerate(words):
        if w&0x9f000000==0x90000000:
            rd=w&31
            imm=((w>>5&0x7ffff)<<2|(w>>29&3))
            if imm>>20&1: imm-=1<<21
            pc=TS+i*4
            page=((pc>>12)+imm)<<12
            for j in range(i+1,min(i+8,n)):
                w2=words[j]
                if w2&0xff000000==0x91000000 and (w2>>5&31)==rd:
                    sh=w2>>22&1; im=(w2>>10&0xfff)<<(12 if sh else 0)
                    a=page+im
                    if a in tg: res.setdefault(a,[]).append(pc)
                    break
                if (words[j]&31)==rd and not (w2&0xff000000==0x91000000): break
    return res
if __name__=='__main__':
    t=[int(x,16) for x in sys.argv[1:]]
    r=xrefs(t)
    for a,l in r.items(): print(hex(a),[hex(x) for x in l])
