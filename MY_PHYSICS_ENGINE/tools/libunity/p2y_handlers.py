import sys
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import p2y_tables as tables, udis
allh={}
for n,a in tables.B.items():
    for off,f in tables.build(a).items(): allh.setdefault(f,[]).append((n,off//8))
starts=sorted(allh)
def vcalls(a,b):
    res=[];last={}
    for pc in range(a,b,4):
        w=udis.word(pc)
        if (w&0xffc00000)==0xf9400000:   # ldr x,[x,#imm]
            last[w&31]=(w>>10&0xfff)*8
        if (w&0xfffffc1f)==0xd63f0000:   # blr
            r=w>>5&31
            if r in last: res.append(last[r])
            else: res.append(-1)
    return res
def floads(a,b):
    c=0
    for pc in range(a,b,4):
        w=udis.word(pc)
        if (w&0xffc00000)==0xbd400000: c+=1  # ldr s
    return c
out=[]
for i,s in enumerate(starts):
    e=starts[i+1] if i+1<len(starts) else s+0x200
    e=min(e,s+0x300)
    out.append((s,e,allh[s],vcalls(s,e),floads(s,e)))
if __name__=='__main__':
    for s,e,tn,v,f in out:
        print('%x %s calls=%s fl=%d'%(s,tn,[hex(x) if x>=0 else '?' for x in v],f))
