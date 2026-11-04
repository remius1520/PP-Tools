import struct, sys
sys.path.insert(0,'/home/claude/u/tools')
import sf
def al(q): return (q+3)&~3
def parse_mesh(b):
    q=0; n,=struct.unpack_from('<I',b,q); q+=4; name=b[q:q+n].decode(errors='replace'); q=al(q+n)
    ns,=struct.unpack_from('<I',b,q); q+=4; subs=[]
    for _ in range(ns):
        subs.append(struct.unpack_from('<IIiIII6f',b,q)); q+=48
    # blendshape data: 4 arrays
    for _ in range(4):
        c,=struct.unpack_from('<I',b,q); q+=4
        if c: raise Exception('blendshapes present')
    c,=struct.unpack_from('<I',b,q); q+=4+64*c      # bindpose
    c,=struct.unpack_from('<I',b,q); q+=4+4*c       # bone name hashes
    q+=4                                            # root bone hash
    c,=struct.unpack_from('<I',b,q); q+=4+24*c      # bones aabb
    c,=struct.unpack_from('<I',b,q); q+=4+4*c       # var bone weights
    comp,rd,kv,ki=b[q:q+4]; q+=4
    ifmt,=struct.unpack_from('<i',b,q); q+=4
    c,=struct.unpack_from('<I',b,q); q+=4; ib=b[q:q+c]; q=al(q+c)
    vc,=struct.unpack_from('<I',b,q); q+=4
    nch,=struct.unpack_from('<I',b,q); q+=4; ch=[tuple(b[q+4*i:q+4*i+4]) for i in range(nch)]; q+=4*nch
    c,=struct.unpack_from('<I',b,q); q+=4; vd=b[q:q+c]; q=al(q+c)
    return dict(name=name,subs=subs,comp=comp,readable=rd,ifmt=ifmt,ib=ib,vc=vc,ch=ch,vd=vd,rest=q)
def geometry(m):
    # positions: channel 0; format 0=float32; dim; stream/offset
    ch=m['ch']; s,off,fmt,dim=ch[0]
    if dim&0xf==0: return None
    dim&=0xf
    # stride per stream
    stride={}
    for st in sorted(set(c[0] for c in ch if c[3]&0xf)):
        stride[st]=sum(({0:4,1:2,2:1,3:1,4:1,5:1,6:2,7:2,8:4,9:4,10:2}.get(c[2],4))*(c[3]&0xf) for c in ch if c[0]==st and c[3]&0xf)
    # stream base offsets (aligned 16)
    base={}; cur=0
    for st in sorted(stride):
        base[st]=cur; cur=(cur+stride[st]*m['vc']+15)&~15
    pos=[]
    for i in range(m['vc']):
        p=base[s]+i*stride[s]+off
        pos.append(struct.unpack_from('<%df'%dim,m['vd'],p))
    sz=2 if m['ifmt']==0 else 4
    idx=list(struct.unpack('<%d%s'%(len(m['ib'])//sz,'H' if sz==2 else 'I'),m['ib']))
    return pos,idx
if __name__=='__main__':
    d,objs,ver,uv,e=sf.rd('/home/claude/u/out_data/sharedassets0.assets')
    O={o[0]:o for o in objs}
    for pid in map(int,sys.argv[1:]):
        _,off,size,cid=O[pid]; b=d[off:off+size]
        m=parse_mesh(b); print(pid,m['name'],'verts',m['vc'],'ib',len(m['ib']),'readable',m['readable'],'ch',m['ch'][:3],'subs',m['subs'][:1])
        g=geometry(m)
        if g: print('  first verts',[tuple(round(x,3) for x in p) for p in g[0][:4]],'tris',len(g[1])//3)
