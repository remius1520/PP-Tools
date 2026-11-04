import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
D=udis.D; TS=udis.TS; TO=udis.TO; TSZ=0x10d6b80
n=TSZ//4
W=struct.unpack('<%dI'%n,D[TO:TO+n*4])
# scalar fp32: fmsub (1f 00 8x?)  fmadd/fmsub s: 0x1f000000 base, type=0 (single) bits23:22=00, o1=bit21, o0=bit15
def is_fmsub_s(w): return (w&0xffe08000)==0x1f008000
def is_fmax_s(w):
    # fp data-proc 2 source: 0x1e200800 | opcode<<12 ; fmax=4, fmaxnm=6
    return (w&0xff20fc00) in (0x1e204800,0x1e206800)
def is_fnmsub(w): return (w&0xffe08000)==0x1f208000
hits=[]
for i,w in enumerate(W):
    if is_fmsub_s(w) or is_fnmsub(w):
        for j in range(i+1,min(i+6,n)):
            if is_fmax_s(W[j]): hits.append((i,j)); break
print(len(hits))
# cluster: >=2 hits within 80 insns
cl=[]
for a in range(len(hits)):
    for b in range(a+1,len(hits)):
        if 0<hits[b][0]-hits[a][0]<80: cl.append((TS+hits[a][0]*4,TS+hits[b][0]*4))
print(len(cl))
for c in cl[:60]: print('%x %x'%c)
