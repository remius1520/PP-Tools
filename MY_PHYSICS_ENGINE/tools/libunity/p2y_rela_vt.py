import struct,sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
D=udis.D
RO=0x38a8; RS=0xd4148
rel={}
for i in range(RS//24):
    off,info,add=struct.unpack('<QQq',D[RO+i*24:RO+i*24+24])
    if info&0xffffffff==1027: rel[off]=add   # R_AARCH64_RELATIVE
print(len(rel))
TSa=0x5438e0; TE=TSa+0x10d6b80
# build vtable arrays: consecutive 8-byte slots whose addends are in .text
offs=sorted(rel)
arrs=[];cur=[]
for o in offs:
    if TSa<=rel[o]<TE:
        if cur and o==cur[-1]+8: cur.append(o)
        else:
            if len(cur)>=8: arrs.append(cur)
            cur=[o]
    else:
        if len(cur)>=8: arrs.append(cur)
        cur=[]
if len(cur)>=8: arrs.append(cur)
print(len(arrs))
import pickle; pickle.dump((rel,arrs),open('/home/claude/rel.pkl','wb'))
# arrays containing addends within 0x1370000-0x1380000
for a in arrs:
    ad=[rel[o] for o in a]
    if any(0x1370000<=x<0x1380000 for x in ad): print(hex(a[0]),len(a),[hex(x) for x in ad][:6])
