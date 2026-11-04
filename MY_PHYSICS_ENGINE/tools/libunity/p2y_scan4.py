import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
D=udis.D; TS=udis.TS; TO=udis.TO; TSZ=0x10d6b80
n=TSZ//4
W=struct.unpack('<%dI'%n,D[TO:TO+n*4])
M=0xbfe0fc00
def vop(w):
    # vector 3-same fp, size bit sz at 22 (0=single), Q bit30; ignore Q
    for name,base in (('fmul',0x2e20dc00),('fsub',0x0ea0d400),('fadd',0x0e20d400),('fmax',0x0e20f400),('fmaxnm',0x0e20c400)):
        if (w&0xbfe0fc00)==base: return name
    return None
hits=[]
for i in range(n):
    if vop(W[i]) in('fmax','fmaxnm'):
        rn,rm=W[i]>>5&31,W[i]>>16&31
        for j in range(i-1,max(i-4,0),-1):
            if vop(W[j])=='fsub' and (W[j]&31) in(rn,rm):
                for k in range(j-1,max(j-5,0),-1):
                    if vop(W[k])=='fmul' and (W[k]&31) in((W[j]>>5&31),(W[j]>>16&31)):
                        hits.append(i);break
                break
print(len(hits))
cl=[(TS+a*4,TS+b*4) for ai,a in enumerate(hits) for b in hits[ai+1:] if 0<b-a<400]
print(len(cl))
for c in cl: print('%x %x'%c)
