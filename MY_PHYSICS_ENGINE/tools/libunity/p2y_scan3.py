import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
D=udis.D; TS=udis.TS; TO=udis.TO; TSZ=0x10d6b80
n=TSZ//4
W=struct.unpack('<%dI'%n,D[TO:TO+n*4])
def op2(w):
    if (w&0xff200c00)==0x1e200800 and (w>>22&3)==0: return w>>12&15
    return None
hits=[]
for i in range(n):
    if op2(W[i]) in (4,6):
        # previous 4 insns contain fsub whose result is an operand of fmax, and a fmul before that
        rd_sub=None
        for j in range(i-1,max(i-5,0),-1):
            if op2(W[j])==3 and (W[j]&31) in (W[i]>>5&31, W[i]>>16&31):
                for k in range(j-1,max(j-5,0),-1):
                    if op2(W[k])==0 and (W[k]&31) in (W[j]>>5&31, W[j]>>16&31):
                        hits.append(i); break
                break
print(len(hits))
# clusters of 2 within 60 insns
cl=[(TS+a*4,TS+b*4) for ai,a in enumerate(hits) for b in hits[ai+1:] if 0<b-a<60]
print(len(cl))
for c in cl: print('%x %x'%c)
