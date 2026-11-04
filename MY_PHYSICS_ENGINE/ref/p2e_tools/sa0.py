import os; W0=os.environ.get('PP2_WORK','.')
import sys,struct; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import sf
sf.common_str=lambda off: sf.COMMON[off:sf.COMMON.index('\0',off)] if off<len(sf.COMMON) else '?%d'%off
d=open(W0+'/dat/sharedassets0.assets','rb').read()
F=sf.SF(d)
from collections import defaultdict,Counter
byc=defaultdict(list); byid={}
for o in F.objs: byc[o.cid].append(o); byid[o.pid]=o
def raw(o): return d[o.off:o.off+o.size]
def words(o): 
    b=raw(o); return struct.unpack('<%dI'%(len(b)//4),b[:len(b)//4*4])
def pptr(b,p): 
    return struct.unpack_from('<iq',b,p)
def rstr(b,p):
    n=struct.unpack_from('<I',b,p)[0]; s=b[p+4:p+4+n].decode('utf8','replace'); return s,(p+4+n+3)//4*4
def go(o):
    b=raw(o); n=struct.unpack_from('<I',b,0)[0]; p=4
    comps=[]
    for i in range(n): comps.append(pptr(b,p)[1]); p+=12
    layer=struct.unpack_from('<I',b,p)[0]; p+=4
    name,p=rstr(b,p)
    tag=struct.unpack_from('<H',b,p)[0]; act=b[p+2]
    return dict(comps=comps,layer=layer,name=name,tag=tag,active=act)
def tr(o):
    b=raw(o)
    g=pptr(b,0)[1]; q=struct.unpack_from('<4f',b,12); p=struct.unpack_from('<3f',b,28); s=struct.unpack_from('<3f',b,40)
    n=struct.unpack_from('<I',b,52)[0]; ch=[pptr(b,56+12*i)[1] for i in range(n)]
    fa=pptr(b,56+12*n)[1]
    return dict(go=g,q=q,p=p,s=s,ch=ch,father=fa)

def parse_mesh(o, cooking=True, verbose=False):
    b=raw(o); p=0
    def u32():
        nonlocal p; v=struct.unpack_from('<I',b,p)[0]; p+=4; return v
    def i32():
        nonlocal p; v=struct.unpack_from('<i',b,p)[0]; p+=4; return v
    def f32s(n):
        nonlocal p; v=struct.unpack_from('<%df'%n,b,p); p+=4*n; return v
    def al():
        nonlocal p; p=(p+3)//4*4
    m={}
    L=u32(); m['name']=b[p:p+L].decode('utf8','replace'); p+=L; al()
    n=u32(); subs=[]
    for _ in range(n):
        fb,ic,top,bv,fv,vc=struct.unpack_from('<IIiIII',b,p); p+=24; aabb=f32s(6)
        subs.append(dict(firstByte=fb,indexCount=ic,topology=top,baseVertex=bv,firstVertex=fv,vertexCount=vc,aabb=aabb))
    m['subs']=subs
    # BlendShapeData
    n=u32(); p+=40*n
    n=u32()
    for _ in range(n): p+=4+4+2; al()
    n=u32()
    for _ in range(n):
        L=u32(); p+=L; al(); p+=12
    n=u32(); p+=4*n
    n=u32(); p+=64*n   # bind pose
    n=u32(); p+=4*n    # bone name hashes
    p+=4               # root bone hash
    n=u32(); p+=24*n   # bones aabb
    n=u32(); p+=4*n    # variable bone count weights
    m['compression']=b[p]; m['readable']=b[p+1]; m['keepV']=b[p+2]; m['keepI']=b[p+3]; p+=4
    m['indexFormat']=i32()
    n=u32(); m['ib_off']=p; m['ib_len']=n; p+=n; al()
    m['vcount']=u32()
    nch=u32(); ch=[]
    for _ in range(nch): ch.append(tuple(b[p:p+4])); p+=4
    m['channels']=ch
    n=u32(); m['vd_off']=p; m['vd_len']=n; p+=n; al()
    # compressed mesh
    cm_start=p
    for _ in range(5):
        p+=4+4+4; n=u32(); p+=n; al(); p+=1; al()
    for _ in range(5):
        p+=4; n=u32(); p+=n; al(); p+=1; al()
    p+=4
    m['aabb']=f32s(6)
    m['usage']=i32()
    if cooking: m['cooking']=i32()
    n=u32(); m['bcm_off']=p; m['bcm_len']=n; p+=n; al()
    n=u32(); m['btm_off']=p; m['btm_len']=n; p+=n; al()
    m['metrics']=f32s(2)
    al()
    m['stream']=struct.unpack_from('<QI',b,p); p+=12
    L=u32(); m['stream_path']=b[p:p+L].decode(); p+=L; al()
    m['end']=p; m['size']=len(b)
    return m
