import os; W0=os.environ.get('PP2_WORK','.')
import sys,struct,json,math; sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from sa0 import *
import numpy as np
RES=open(W0+'/dat/sharedassets0.assets.resS','rb').read()
G={o.pid:go(o) for o in byc[1]}
T={o.pid:tr(o) for o in byc[4]}
goT={t['go']:k for k,t in T.items()}
TAGS={0:'Untagged',20000:'Edge',20001:'Ball',20002:'PocketInside',20003:'Table',20004:'Cutout',20005:'Black',20006:'Scroll',20007:'Cuebase',20008:'CircleFoulCenter'}
LAYERS={0:'Default',3:'AimCast',7:'TopLayer',8:'Cue',10:'Ball',11:'Edge',15:'Level'}
MATS={302:'Ball_Physics',303:'EdgePhysics_Snooker',304:'Edges_Physics',305:'Pocket_Physics',306:'Table_Physics',0:None}
def qmat(q):
    x,y,z,w=q; n=math.sqrt(x*x+y*y+z*z+w*w); x,y,z,w=x/n,y/n,z/n,w/n
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def local(t):
    M=np.eye(4); M[:3,:3]=qmat(t['q'])@np.diag(t['s']); M[:3,3]=t['p']; return M
_wc={}
def world(k):
    if k in _wc: return _wc[k]
    t=T[k]; M=local(t)
    if t['father'] and t['father'] in T: M=world(t['father'])@M
    _wc[k]=M; return M
def active_chain(k):
    while k:
        g=G[T[k]['go']]
        if not g['active']: return False
        k=T[k]['father']
    return True
def root_of(k):
    while T[k]['father'] and T[k]['father'] in T: k=T[k]['father']
    return k
def mesh_tris(mpid):
    o=byid.get(mpid)
    if not o or o.cid!=43: return None
    m=parse_mesh(o,True)
    b=raw(o)
    off,size=m['stream']
    assert m['stream_path'].endswith('sharedassets0.assets.resS')
    vb=RES[off:off+size]
    stride=size//m['vcount']
    pos=np.frombuffer(vb,dtype=np.uint8).reshape(m['vcount'],stride)[:,:12].copy().view('<f4').reshape(-1,3)
    ib=b[m['ib_off']:m['ib_off']+m['ib_len']]
    idx=np.frombuffer(ib,dtype='<u2' if m['indexFormat']==0 else '<u4').astype(np.int64)
    tris=[]
    for s in m['subs']:
        if s['topology']!=0: continue
        a=idx[s['firstByte']//(2 if m['indexFormat']==0 else 4):][:s['indexCount']]+s['baseVertex']
        tris.append(a.reshape(-1,3))
    return pos,np.concatenate(tris) if tris else np.zeros((0,3),int),m['name']
def collect():
    out=[]
    for o in byc[64]+byc[65]:
        w=words(o); g=G[w[1]]; k=goT.get(w[1])
        isbox=(o.cid==65)
        d=dict(kind='Box' if isbox else 'Mesh',go=g['name'],tag=TAGS.get(g['tag'],g['tag']),layer=LAYERS.get(g['layer'],g['layer']),
               material=MATS.get(w[4]),trigger=bool(w[9]&0xff),enabled=bool(w[10]&0xff),goActive=bool(g['active']),chainActive=active_chain(k),
               root=G[T[root_of(k)]['go']]['name'],rootId=root_of(k))
        W=world(k)
        d['world']=W.tolist()
        if isbox:
            d['size']=struct.unpack_from('<3f',raw(o),44); d['center']=struct.unpack_from('<3f',raw(o),56)
        else:
            d['convex']=bool(w[11]&0xff); d['cooking']=w[12]; d['meshId']=w[14]
        out.append(d)
    return out
if __name__=='__main__':
    C=collect()
    print(len(C))
    by=defaultdict(list)
    for c in C: by[c['root']].append(c)
    for r,l in by.items():
        print(r,len(l),Counter((c['tag'],c['layer'],c['trigger'],c['enabled'],c['chainActive'],c['kind'],c.get('convex')) for c in l).most_common(12))
