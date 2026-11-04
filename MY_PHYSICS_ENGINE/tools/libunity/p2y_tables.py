import sys,struct
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
def sx(v,b): return v-(1<<b) if v>>(b-1)&1 else v
def build(start,maxn=6000):
    regs={}; out={}
    pc=start
    for k in range(maxn):
        w=udis.word(pc)
        if w&0x9f000000==0x10000000:  # adr
            regs[w&31]=pc+sx((w>>5&0x7ffff)<<2|(w>>29&3),21)
        elif w&0xffc00000==0xa9000000 or (w&0xffc00000)==0xa9000000:
            pass
        # stp x,x,[x0,#imm]
        if (w&0xffc00000)==0xa9000000:
            rt=w&31; rt2=w>>10&31; rn=w>>5&31; imm=sx(w>>15&0x7f,7)*8
            if rn==0 and rt in regs and rt2 in regs: out[imm]=regs[rt]; out[imm+8]=regs[rt2]
        if (w&0xffc00000)==0xf9000000:  # str x,[x0,#imm]
            rt=w&31; rn=w>>5&31; imm=(w>>10&0xfff)*8
            if rn==0 and rt in regs: out[imm]=regs[rt]
        if w==0xd65f03c0: break
        pc+=4
    return out
B={'t0':0xc09264,'t1':0xc0b2b8,'t2':0xc05924,'t3':0xc12d58,'t4':0xc08dc8,'t5':0xc06b28,'t6':0xc0e4c8}
if __name__=='__main__':
    for n,a in B.items():
        t=build(a)
        print(n,hex(a),len(t),'entries; idx->fn:',' '.join('%d:%x'%(o//8,f) for o,f in sorted(t.items())))
