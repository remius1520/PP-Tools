import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
D=udis.D; TS=udis.TS; TO=udis.TO; TSZ=0x10d6b80
n=TSZ//4
W=struct.unpack('<%dI'%n,D[TO:TO+n*4])
def isfmax(w): return (w&0xbfe0fc00) in (0x0e20f400,0x0e20c400) or ((w&0xff20fc00) in (0x1e204800,0x1e206800))
hits=[]
for i,w in enumerate(W):
    ok=False
    if (w&0xffc00000)==0xbd400000 and (w>>10&0xfff) in (0x1c,0x1d): ok=True   # ldr s [x,#0x70/0x74]
    if (w&0xffc00000)==0xfd400000 and (w>>10&0xfff)==14: ok=True               # ldr d [x,#0x70]
    if (w&0xffc00000)==0x2d400000 and (w>>15&0x7f)==0x1c: ok=True            # ldp s,s [x,#0x70]
    if ok:
        seg=W[i:i+60]
        if any(isfmax(x) for x in seg): hits.append(TS+i*4)
print(len(hits),[hex(h) for h in hits])
