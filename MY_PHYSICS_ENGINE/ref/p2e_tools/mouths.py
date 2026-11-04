import os; W0=os.environ.get('PP2_WORK','.')
"""Pocket mouths from the real collider meshes: for every pocket, the free interval (ball centre at height yc, radius r, exact 3-D point-triangle distance)
on a line a little beyond the ball-centre limit of the wall the pocket opens onto. Near-horizontal faces (bed / rims) are skipped."""
import json,sys,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from freespace import tri_dist,build
OFF=0.012
def tris_for(key):
    D,tris=build(key); A,B,C=tris[:,0],tris[:,1],tris[:,2]
    n=np.cross(B-A,C-A); nl=np.linalg.norm(n,axis=1); n=n/np.where(nl==0,1,nl)[:,None]
    k=np.abs(n[:,1])<0.985
    return D,A[k],B[k],C[k]
def run_line(A,B,C,pts,r):
    lo=np.minimum(np.minimum(A,B),C); hi=np.maximum(np.maximum(A,B),C)
    pmin=pts.min(0)-r; pmax=pts.max(0)+r
    k=(hi>=pmin).all(1)&(lo<=pmax).all(1)
    d=np.full(len(pts),9.0)
    for i in range(0,len(pts),500): d[i:i+500]=tri_dist(pts[i:i+500],A[k],B[k],C[k]).min(1)
    return d>=r
def interval(free,coords,seed):
    # contiguous free run nearest to seed
    runs=[];i=0;n=len(free)
    while i<n:
        if free[i]:
            j=i
            while j+1<n and free[j+1]: j+=1
            runs.append((coords[i],coords[j])); i=j+1
        else: i+=1
    if not runs: return None
    def dist(r): return 0 if r[0]<=seed<=r[1] else min(abs(seed-r[0]),abs(seed-r[1]))
    return min(runs,key=dist)
def compute(key,yc,r,span=0.22,step=0.001):
    D,A,B,C=tris_for(key); g=D['rails']; zc=0.5*(g['zb']+g['zt']); xc=0.5*(g['xl']+g['xr']); W=g['xr']-g['xl']
    res=[]
    for p in D['pockets']:
        top=p['z']>zc; mid=abs(p['x']-xc)<0.25*W; right=p['x']>xc
        # z-wall (crossing the top/bottom limit line)
        zrow=(g['zt']-r+OFF) if top else (g['zb']+r-OFF)
        xs=np.arange(p['x']-span,p['x']+span,step); pts=np.c_[xs,np.full(len(xs),yc),np.full(len(xs),zrow)]
        iz=interval(run_line(A,B,C,pts,r),xs,p['x'])
        ix=None
        if not mid:
            xcol=(g['xr']-r+OFF) if right else (g['xl']+r-OFF)
            zs=np.arange(p['z']-span,p['z']+span,step); pts=np.c_[np.full(len(zs),xcol),np.full(len(zs),yc),zs]
            ix=interval(run_line(A,B,C,pts,r),zs,p['z'])
        res.append(dict(top=bool(top),mid=bool(mid),right=bool(right),openX=iz,openZ=ix))
    return res
if __name__=='__main__':
    D=json.load(open(W0+'/data/pp2_tables.json'))
    for key in D:
        r=0.5*D[key]['ballScale']; yc=D[key]['ballY'] if key!='Lvl1_4' else np.mean([-0.032])
        print(key,'r=%.4f yc=%.3f'%(r,yc))
        for m in compute(key,yc,r): print('  ',m)
