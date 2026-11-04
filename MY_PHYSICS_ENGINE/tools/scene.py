import sys, struct, json, math
sys.path.insert(0,'/home/claude/u/tools')
import sf
d, objs, ver, uv, e = sf.rd('/home/claude/u/out_data/sharedassets0.assets')
O = {o[0]:o for o in objs}
def blob(pid): _,off,size,cid=O[pid]; return d[off:off+size]
def pptr(b,o): f,p=struct.unpack_from('<iq',b,o); return f,p
def gameobject(pid):
    b=blob(pid); n,=struct.unpack_from('<i',b,0); q=4; comps=[]
    for _ in range(n):
        f,p=pptr(b,q); q+=12; comps.append(p)
    layer,=struct.unpack_from('<I',b,q); q+=4
    ln,=struct.unpack_from('<I',b,q); q+=4; name=b[q:q+ln].decode(errors='replace'); q=(q+ln+3)&~3
    tag,=struct.unpack_from('<H',b,q)
    return dict(comps=comps,layer=layer,name=name,tag=tag)
def transform(pid):
    b=blob(pid); go=pptr(b,0)[1]
    r=struct.unpack_from('<4f',b,12); p=struct.unpack_from('<3f',b,28); s=struct.unpack_from('<3f',b,40)
    n,=struct.unpack_from('<i',b,52); q=56+12*n; fa=pptr(b,q)[1]
    return dict(go=go,r=r,p=p,s=s,father=fa)
GO={p:gameobject(p) for p,_,_,c in objs if c==1}
TR={p:transform(p) for p,_,_,c in objs if c==4}
GO2TR={t['go']:p for p,t in TR.items()}
def qmul(a,b):
    x1,y1,z1,w1=a;x2,y2,z2,w2=b
    return (w1*x2+x1*w2+y1*z2-z1*y2, w1*y2-x1*z2+y1*w2+z1*x2, w1*z2+x1*y2-y1*x2+z1*w2, w1*w2-x1*x2-y1*y2-z1*z2)
def qrot(q,v):
    x,y,z,w=q; vx,vy,vz=v
    t=(2*(y*vz-z*vy),2*(z*vx-x*vz),2*(x*vy-y*vx))
    return (vx+w*t[0]+(y*t[2]-z*t[1]), vy+w*t[1]+(z*t[0]-x*t[2]), vz+w*t[2]+(x*t[1]-y*t[0]))
def world(tp):
    chain=[]
    while tp in TR: chain.append(TR[tp]); tp=TR[tp]['father']
    pos=(0,0,0); rot=(0,0,0,1); sc=(1,1,1)
    for t in reversed(chain):
        lp=tuple(t['p'][i]*sc[i] for i in range(3))
        pos=tuple(pos[i]+qrot(rot,lp)[i] for i in range(3))
        rot=qmul(rot,t['r']); sc=tuple(sc[i]*t['s'][i] for i in range(3))
    return pos,rot,sc
def path(go):
    names=[]; tp=GO2TR.get(go)
    while tp in TR: names.append(GO[TR[tp]['go']]['name']); tp=TR[tp]['father']
    return '/'.join(reversed(names))
res={'rigidbody':[], 'box':[], 'sphere':[], 'mesh':[]}
for pid,off,size,cid in objs:
    if cid not in (54,64,65,135): continue
    b=blob(pid); go=pptr(b,0)[1]; g=GO[go]; pos,rot,sc=world(GO2TR[go])
    rec=dict(id=pid,go=g['name'],path=path(go),layer=g['layer'],tag=g['tag'],pos=pos,rot=rot,scale=sc)
    if cid==54: res['rigidbody'].append(rec)
    else:
        rec['material']=pptr(b,12)[1]; rec['trigger']=b[36]; rec['enabled']=b[40]
        if cid==64: rec['convex']=b[44]; rec['cook']=struct.unpack_from('<i',b,48)[0]; rec['mesh']=pptr(b,52)[1]; res['mesh'].append(rec)
        elif cid==65: rec['size']=struct.unpack_from('<3f',b,44); rec['center']=struct.unpack_from('<3f',b,56); res['box'].append(rec)
        else: rec['radius']=struct.unpack_from('<f',b,44)[0]; rec['center']=struct.unpack_from('<3f',b,48); res['sphere'].append(rec)
json.dump(res,open('/home/claude/u/scene_sharedassets0.json','w'),indent=1)
from collections import Counter
print('rigidbody',[(r['path'],[round(x,4) for x in r['pos']],[round(x,4) for x in r['scale']],r['layer'],r['tag']) for r in res['rigidbody']])
print('sphere',[(r['path'],r['radius'],[round(x,4) for x in r['scale']]) for r in res['sphere']])
print('box',[(r['path'],r['trigger'],r['material'],[round(x,3) for x in r['size']],[round(x,3) for x in r['scale']],r['layer'],r['tag']) for r in res['box']])
c=Counter((r['material'],r['trigger'],r['convex'],r['layer'],r['tag'],r['path'].split('/')[0]) for r in res['mesh']); print('mesh groups'); 
for k,v in c.most_common(12): print(v,k)
print('distinct meshes',len({r['mesh'] for r in res['mesh']}))
