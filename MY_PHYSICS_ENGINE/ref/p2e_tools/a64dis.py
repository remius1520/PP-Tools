import struct,pickle,sys
M=pickle.load(open('/home/claude/rvamap.pkl','rb'))
SO=open('/home/claude/so/libil2cpp.so','rb')
def word(rva):
    SO.seek(rva-0x4000); return struct.unpack('<I',SO.read(4))[0]
def sx(v,b): return v-(1<<b) if v>>(b-1)&1 else v
CC=['eq','ne','hs','lo','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','al','nv']
def X(n,sf=1,sp=False):
    if n==31: return ('sp' if sp else ('xzr' if sf else 'wzr'))
    return ('x%d' if sf else 'w%d')%n
def F(n,t): return {0:'s',1:'d',3:'h'}.get(t,'v')+str(n)
def fimm(i):
    sign=-1 if i>>7&1 else 1; b=i>>6&1; cd=i>>4&3; frac=(i&15)/16.0
    ex=(cd-3) if b else (cd+1)
    return sign*(1+frac)*(2.0**ex)
def dis(pc,w,sym=True):
    def tgt(t):
        r=M.get(t); return '%x%s'%(t,(' <%s::%s>'%(r[0],r[1][:60])) if r else '')
    if w>>26==0b100101: return 'bl  '+tgt(pc+sx(w&0x3ffffff,26)*4)
    if w>>26==0b000101: return 'b   '+tgt(pc+sx(w&0x3ffffff,26)*4)
    if w&0xff000010==0x54000000: return 'b.%s %x'%(CC[w&15],pc+sx(w>>5&0x7ffff,19)*4)
    if w&0x7e000000==0x34000000:
        sf=w>>31; return '%s %s, %x'%('cbnz' if w>>24&1 else 'cbz',X(w&31,sf),pc+sx(w>>5&0x7ffff,19)*4)
    if w&0x7e000000==0x36000000:
        b=(w>>31<<5)|(w>>19&31); return '%s %s, #%d, %x'%('tbnz' if w>>24&1 else 'tbz',X(w&31,1 if b>=32 else 0),b,pc+sx(w>>5&0x3fff,14)*4)
    if w==0xd65f03c0: return 'ret'
    if w&0xfffffc1f==0xd61f0000: return 'br  x%d'%(w>>5&31)
    if w&0xfffffc1f==0xd63f0000: return 'blr x%d'%(w>>5&31)
    if w&0x9f000000==0x90000000: return 'adrp %s, %x'%(X(w&31),((pc>>12)+sx((w>>5&0x7ffff)<<2|(w>>29&3),21))<<12)
    if w&0x9f000000==0x10000000: return 'adr %s, %x'%(X(w&31),pc+sx((w>>5&0x7ffff)<<2|(w>>29&3),21))
    # add/sub imm
    if w&0x1f000000==0x11000000:
        sf=w>>31;op=w>>30&1;S=w>>29&1;sh=w>>22&1;imm=(w>>10&0xfff)<<(12 if sh else 0);rn=w>>5&31;rd=w&31
        if S and rd==31: return 'cmp%s %s, #0x%x'%('' if op else 'n',X(rn,sf,True),imm)
        return '%s%s %s, %s, #0x%x'%('sub' if op else 'add','s' if S else '',X(rd,sf,not S),X(rn,sf,True),imm)
    # add/sub shifted reg
    if w&0x1f200000==0x0b000000:
        sf=w>>31;op=w>>30&1;S=w>>29&1;rm=w>>16&31;rn=w>>5&31;rd=w&31;sh=['lsl','lsr','asr','ror'][w>>22&3];a=w>>10&63
        n=('sub' if op else 'add')+('s' if S else '')
        if S and rd==31: return '%s %s, %s'%('cmp' if op else 'cmn',X(rn,sf),X(rm,sf))+(', %s #%d'%(sh,a) if a else '')
        return '%s %s, %s, %s'%(n,X(rd,sf),X(rn,sf),X(rm,sf))+(', %s #%d'%(sh,a) if a else '')
    # logical shifted reg
    if w&0x1f000000==0x0a000000:
        sf=w>>31;opc=w>>29&3;N=w>>21&1;rm=w>>16&31;rn=w>>5&31;rd=w&31;a=w>>10&63
        if opc==1 and rn==31 and not N and not a: return 'mov %s, %s'%(X(rd,sf),X(rm,sf))
        n=['and','orr','eor','ands'][opc]+('n' if N else '')
        return '%s %s, %s, %s'%(n,X(rd,sf),X(rn,sf),X(rm,sf))+(' #%d'%a if a else '')
    # mov wide
    if w&0x1f800000==0x12800000:
        sf=w>>31;opc=w>>29&3;hw=w>>21&3;imm=w>>5&0xffff
        return '%s %s, #0x%x%s'%({0:'movn',2:'movz',3:'movk'}.get(opc,'?'),X(w&31,sf),imm,(', lsl #%d'%(hw*16) if hw else ''))
    # logical imm (just mark)
    if w&0x1f800000==0x12000000: return 'logical-imm .word %08x'%w
    # csel/csinc
    if w&0x1fe00000==0x1a800000:
        sf=w>>31;op=w>>30&1;o2=w>>10&3;rm=w>>16&31;rn=w>>5&31;rd=w&31;c=CC[w>>12&15]
        nm={(0,0):'csel',(0,1):'csinc',(1,0):'csinv',(1,1):'csneg'}[(op,o2&1)]
        if nm=='csinc' and rm==31 and rn==31: return 'cset %s, %s'%(X(rd,sf),CC[(w>>12&15)^1])
        return '%s %s, %s, %s, %s'%(nm,X(rd,sf),X(rn,sf),X(rm,sf),c)
    # ldr/str imm unsigned & unscaled & pre/post
    if w&0x3b000000==0x39000000 or w&0x3b200c00 in (0x38000000,0x38000400,0x38000c00):
        sz=w>>30;V=w>>26&1;opc=w>>22&3;rn=w>>5&31;rt=w&31
        if w&0x3b000000==0x39000000: off=(w>>10&0xfff)<<sz; mode='u'
        else: off=sx(w>>12&0x1ff,9); mode={0:'s',1:'post',3:'pre'}[w>>10&3]
        if V:
            sc=sz if opc<2 else 4; t={0:'b',1:'h',2:'s',3:'d',4:'q'}[sc]; reg='%s%d'%(t,rt)
            ld=opc&1
        else:
            ld=opc!=0
            if opc>=2: nm='ldrs'+'bhw'[sz] ; reg=X(rt,opc==2)
            else: nm=('ldr' if ld else 'str')+('' if sz>=2 else 'bh'[sz]); reg=X(rt,sz==3)
        nm=('ldr' if ld else 'str') if V else nm
        if mode=='post': return '%s %s, [%s], #%d'%(nm,reg,X(rn,1,True),off)
        if mode=='pre': return '%s %s, [%s, #%d]!'%(nm,reg,X(rn,1,True),off)
        return '%s %s, [%s, #0x%x]'%(nm+('u' if mode=='s' else ''),reg,X(rn,1,True),off) if off>=0 else '%s %s, [%s, #-0x%x]'%(nm,reg,X(rn,1,True),-off)
    # ldr register offset
    if w&0x3b200c00==0x38200800:
        sz=w>>30;V=w>>26&1;opc=w>>22&3;rm=w>>16&31;rn=w>>5&31;rt=w&31
        if V: reg='%s%d'%({0:'b',1:'h',2:'s',3:'d',4:'q'}[sz if opc<2 else 4],rt); nm='ldr' if opc&1 else 'str'
        else: reg=X(rt,sz==3); nm='ldr' if opc else 'str'
        return '%s %s, [%s, %s]'%(nm,reg,X(rn,1,True),X(rm,1))
    # ldp/stp
    if w&0x3a000000==0x28000000:
        opc=w>>30;V=w>>26&1;typ=w>>23&3;L=w>>22&1;imm=sx(w>>15&0x7f,7);rt2=w>>10&31;rn=w>>5&31;rt=w&31
        if V: sc=2+opc; reg=lambda n:'%s%d'%({2:'s',3:'d',4:'q'}[sc],n)
        else: sc=3 if opc==2 else 2; reg=lambda n:X(n,opc==2)
        o=imm<<sc; nm='ldp' if L else 'stp'
        if typ==1: return '%s %s, %s, [%s], #%d'%(nm,reg(rt),reg(rt2),X(rn,1,True),o)
        if typ==3: return '%s %s, %s, [%s, #%d]!'%(nm,reg(rt),reg(rt2),X(rn,1,True),o)
        return '%s %s, %s, [%s, #%d]'%(nm,reg(rt),reg(rt2),X(rn,1,True),o)
    # FP
    if w&0xff000000 in (0x1e000000,0x9e000000) and w&0x1e000000==0x1e000000:
        t=w>>22&3;rm=w>>16&31;rn=w>>5&31;rd=w&31
        if w&0x5f200000==0x1e200000:
            if w>>10&3==2:
                op=w>>12&15;n={0:'fmul',1:'fdiv',2:'fadd',3:'fsub',4:'fmax',5:'fmin',6:'fmaxnm',7:'fminnm',8:'fnmul'}.get(op,'f?')
                return '%s %s, %s, %s'%(n,F(rd,t),F(rn,t),F(rm,t))
            if w>>10&3==3: return 'fcsel %s, %s, %s, %s'%(F(rd,t),F(rn,t),F(rm,t),CC[w>>12&15])
            if w>>10&3==1: return 'fccmp %s, %s, #%d, %s'%(F(rn,t),F(rm,t),w&15,CC[w>>12&15])
            if w>>10&7==4 and (w>>13&7)==0 and not (w>>16&0x1f)&0 and w&0x1fe0==0x1000>>0 or (w&0xff201fe0)==0x1e201000: return 'fmov %s, #%g'%(F(rd,t),fimm(w>>13&0xff))
            if w>>10&15==8 and (w>>14&3)==0:
                z=w>>3&1; return 'fcmp%s %s, %s'%('e' if w>>4&1 else '',F(rn,t),'#0.0' if z else F(rm,t))
            if w>>10&0x1f==0x10:
                op=w>>15&0x3f;n={0:'fmov',1:'fabs',2:'fneg',3:'fsqrt',4:'fcvt-s',5:'fcvt-d',7:'fcvt-h',8:'frintn',9:'frintp',10:'frintm',11:'frintz',12:'frinta',14:'frintx',15:'frinti'}.get(op,'f1?%d'%op)
                if op in(4,5,7): d={4:0,5:1,7:3}[op]; return 'fcvt %s, %s'%(F(rd,d),F(rn,t))
                return '%s %s, %s'%(n,F(rd,t),F(rn,t))
            if w>>10&0x3f==0:
                sf=w>>31;rmode=w>>19&3;opc=w>>16&7
                if opc==6: return 'fmov %s, %s'%(X(rd,sf),F(rn,t))
                if opc==7: return 'fmov %s, %s'%(F(rd,t),X(rn,sf))
                if opc==2 and rmode==0: return 'scvtf %s, %s'%(F(rd,t),X(rn,sf))
                if opc==0: return 'fcvt%s %s, %s'%({0:'n',1:'p',2:'m',3:'z'}[rmode]+'s',X(rd,sf),F(rn,t))
                if opc==1: return 'fcvt%su %s, %s'%({0:'n',1:'p',2:'m',3:'z'}[rmode],X(rd,sf),F(rn,t))
        if w&0xff000000==0x1f000000:
            ra=w>>10&31;o1=w>>21&1;o0=w>>15&1
            n={(0,0):'fmadd',(0,1):'fmsub',(1,0):'fnmadd',(1,1):'fnmsub'}[(o1,o0)]
            return '%s %s, %s, %s, %s'%(n,F(rd,t),F(rn,t),F(rm,t),F(ra,t))
    return '.word %08x'%w
import sys
sys.path.insert(0,'/home/claude')
from rd import f32,f64,u32
def dump(a,b):
    pg={}
    for pc in range(a,b,4):
        w=word(pc); t=dis(pc,w); note=''
        if w&0x9f000000==0x90000000:
            pg[w&31]=((pc>>12)+sx((w>>5&0x7ffff)<<2|(w>>29&3),21))<<12
        m=None
        if w&0x3b000000==0x39000000 and (w>>5&31) in pg:
            sz=w>>30; off=(w>>10&0xfff)<<sz; va=pg[w>>5&31]+off
            V=w>>26&1
            try:
                if V and sz==2: note='   ; =%g'%f32(va)
                elif V and sz==3: note='   ; =%g'%f64(va)
                elif not V and sz==2: note='   ; =0x%x'%u32(va)
            except Exception as e: note='   ; ?'
        print('%x: %08x  %s%s'%(pc,w,t,note))
if __name__=='__main__': dump(int(sys.argv[1],16),int(sys.argv[2],16))
