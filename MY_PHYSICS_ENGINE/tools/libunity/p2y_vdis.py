import sys,struct
import os; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import udis
def vname(n,q,sz=0): 
    return 'v%d.%s'%(n,{(0,0):'2s',(1,0):'4s',(1,1):'2d',(0,1):'1d'}[(q,sz)])
def vdis(pc,w):
    q=w>>30&1
    # 3-same fp  (U,size,opcode)
    if (w&0x9f200400)==0x0e200400:
        U=w>>29&1; size=w>>22&3; opc=w>>11&31; rm=w>>16&31; rn=w>>5&31; rd=w&31
        if opc in (0x18,0x19,0x1a,0x1b,0x1c,0x1e,0x1f) or opc>=0x18:
            sz=size&1; hi=size>>1
            tbl={(0,0,0x18):'fmaxnm',(0,0,0x19):'fmla',(0,0,0x1a):'fadd',(0,0,0x1b):'fmulx',(0,0,0x1c):'fcmeq',(0,0,0x1e):'fmax',(0,0,0x1f):'frecps',
                 (0,1,0x18):'fminnm',(0,1,0x19):'fmls',(0,1,0x1a):'fsub',(0,1,0x1e):'fmin',(0,1,0x1f):'frsqrts',
                 (1,0,0x18):'fmaxnmp',(1,0,0x1a):'faddp',(1,0,0x1b):'fmul',(1,0,0x1c):'fcmge',(1,0,0x1d):'facge',(1,0,0x1e):'fmaxp',(1,0,0x1f):'fdiv',
                 (1,1,0x1a):'fabd',(1,1,0x1c):'fcmgt',(1,1,0x1d):'facgt'}
            nm=tbl.get((U,hi,opc))
            if nm: return '%s %s, %s, %s'%(nm,vname(rd,q,sz),vname(rn,q,sz),vname(rm,q,sz))
        if (U,size,opc)==(0,2,3): return 'orr/mov %s, %s, %s'%(vname(rd,q),vname(rn,q),vname(rm,q))
        if (U,size,opc)==(0,0,3): return 'and v%d, v%d, v%d'%(rd,rn,rm)
        if (U,size,opc)==(1,0,3): return 'eor v%d, v%d, v%d'%(rd,rn,rm)
        if (U,size,opc)==(0,1,3): return 'bic v%d, v%d, v%d'%(rd,rn,rm)
        if (U,size,opc)==(1,3,3): return 'bif v%d, v%d, v%d'%(rd,rn,rm)
        if (U,size,opc)==(1,2,3): return 'bit v%d, v%d, v%d'%(rd,rn,rm)
        if (U,size,opc)==(1,1,3): return 'bsl v%d, v%d, v%d'%(rd,rn,rm)
    # by element fp: 0x0f/0x4f /0x2f /0x6f with opcode 0001(fmla) 0101(fmls) 1001(fmul)
    if (w&0x9f000400)==0x0f000000:
        U=w>>29&1; size=w>>22&3; L=w>>21&1; M=w>>20&1; rm=w>>16&15; opc=w>>12&15; H=w>>11&1; rn=w>>5&31; rd=w&31
        if size in (2,3) and opc in (1,5,9):
            idx=(H<<1|L) if size==2 else H
            nm={(0,1):'fmla',(0,5):'fmls',(0,9):'fmul',(1,9):'fmulx'}.get((U,opc))
            if nm: return '%s %s, %s, v%d.%s[%d]'%(nm,vname(rd,q,size&1),vname(rn,q,size&1),M<<4|rm,'s' if size==2 else 'd',idx)
    # fmov/movi vector immediate
    if (w&0x9ff80400)==0x0f000400:
        return 'movi/fmov-imm v%d  (op=%x cmode=%x abc=%x defgh=%x) q=%d'%(w&31,w>>29&1,w>>12&15,w>>16&7,w>>5&31,q)
    # dup element / ins / mov
    if (w&0xbfe0fc00)==0x0e000400: return 'dup v%d, v%d[imm5=%d]'%(w&31,w>>5&31,w>>16&31)
    if (w&0xffe08400)==0x6e000400: return 'ins v%d[..], v%d[..] imm5=%d imm4=%d'%(w&31,w>>5&31,w>>16&31,w>>11&15)
    if (w&0xffe0fc00)==0x4e001c00: return 'ins v%d[..], w%d imm5=%d'%(w&31,w>>5&31,w>>16&31)
    # ldr/str q  (unsigned imm)
    if (w&0x3f000000)==0x3d000000 and w>>30==0 and False: pass
    # ld1/st1 and misc: leave
    return None
def full(pc,w):
    t=vdis(pc,w)
    return t if t else udis.dis(pc,w)
def dump(a,b):
    pg={}
    for pc in range(a,b,4):
        w=udis.word(pc); t=full(pc,w); note=''
        if w&0x9f000000==0x90000000: pg[w&31]=((pc>>12)+udis.sx((w>>5&0x7ffff)<<2|(w>>29&3),21))<<12
        if (w&0x3b000000)==0x39000000 and (w>>5&31) in pg:
            sz=w>>30;V=w>>26&1;off=(w>>10&0xfff)<<(4 if V and (w>>23&1) and sz==0 else sz);va=pg[w>>5&31]+off
            try:
                if V and sz==2: note='   ; =%g'%udis.f32(va)
                elif V and sz==0 and w>>23&1: note='   ; q=%s'%(','.join('%g'%udis.f32(va+4*k) for k in range(4)))
            except Exception: pass
        # q loads/stores: ldr q (size=00,V=1,opc=11)
        if (w&0xffc00000)==0x3dc00000: t='ldr q%d, [x%d, #0x%x]'%(w&31,w>>5&31,(w>>10&0xfff)<<4)
        if (w&0xffc00000)==0x3d800000: t='str q%d, [x%d, #0x%x]'%(w&31,w>>5&31,(w>>10&0xfff)<<4)
        print('%x: %08x  %s%s'%(pc,w,t,note))
if __name__=='__main__': dump(int(sys.argv[1],16),int(sys.argv[2],16))
