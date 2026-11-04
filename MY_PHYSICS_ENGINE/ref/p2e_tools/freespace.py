import os; W0=os.environ.get('PP2_WORK','.')
import json,sys,numpy as np
def tri_dist(P,A,B,C):
    # exact point-triangle distance, P:(n,3), triangles (m,3) each -> (n,m) distances (Ericson)
    P=P[:,None,:]; A=A[None];B=B[None];C=C[None]
    ab=B-A;ac=C-A;ap=P-A
    d1=(ab*ap).sum(-1);d2=(ac*ap).sum(-1)
    bp=P-B;d3=(ab*bp).sum(-1);d4=(ac*bp).sum(-1)
    cp=P-C;d5=(ab*cp).sum(-1);d6=(ac*cp).sum(-1)
    vc=d1*d4-d3*d2;vb=d5*d2-d1*d6;va=d3*d6-d5*d4
    out=np.zeros(d1.shape+(3,))
    # default: interior
    den=va+vb+vc; den=np.where(np.abs(den)<1e-30,1e-30,den)
    v=vb/den;w=vc/den
    q=A+ab*v[...,None]+ac*w[...,None]
    # vertex regions / edge regions
    c1=(d1<=0)&(d2<=0); q=np.where(c1[...,None],A,q)
    c2=(d3>=0)&(d4<=d3); q=np.where(c2[...,None],B,q)
    c3=(d6>=0)&(d5<=d6); q=np.where(c3[...,None],C,q)
    e1=(vc<=0)&(d1>=0)&(d3<=0); t=d1/np.where(d1-d3==0,1e-30,d1-d3); q=np.where((e1&~c1&~c2)[...,None],A+ab*t[...,None],q)
    e2=(vb<=0)&(d2>=0)&(d6<=0); t=d2/np.where(d2-d6==0,1e-30,d2-d6); q=np.where((e2&~c1&~c3)[...,None],A+ac*t[...,None],q)
    e3=(va<=0)&((d4-d3)>=0)&((d5-d6)>=0); t=(d4-d3)/np.where((d4-d3)+(d5-d6)==0,1e-30,(d4-d3)+(d5-d6)); q=np.where((e3&~c2&~c3)[...,None],B+(C-B)*t[...,None],q)
    return np.linalg.norm(P-q,axis=-1)
def build(key,tables=W0+'/data/pp2_tables.json',vertical_only=True):
    D=json.load(open(tables))[key]; tris=[]
    for s in D['solid']:
        V=np.array(s['verts']); T=np.array(s['tris'])
        for a,b,c in T: tris.append((V[a],V[b],V[c]))
    tris=np.array(tris); return D,tris
def free_map(key,yc,r,x0,x1,z0,z1,step=0.004,skip_flat=True):
    D,tris=build(key)
    A,B,C=tris[:,0],tris[:,1],tris[:,2]
    n=np.cross(B-A,C-A); nl=np.linalg.norm(n,axis=1); n=n/np.where(nl==0,1,nl)[:,None]
    keep=np.ones(len(tris),bool)
    if skip_flat: keep&=np.abs(n[:,1])<0.985       # drop (near-)horizontal faces: bed / rims
    lo=tris.min(1);hi=tris.max(1)
    keep&=(lo[:,1]<yc+r)&(hi[:,1]>yc-r)&(hi[:,0]>x0-r)&(lo[:,0]<x1+r)&(hi[:,2]>z0-r)&(lo[:,2]<z1+r)
    A,B,C=A[keep],B[keep],C[keep]
    xs=np.arange(x0,x1,step);zs=np.arange(z0,z1,step)
    G=np.array([(x,yc,z) for z in zs for x in xs])
    dmin=np.full(len(G),9.0)
    for i in range(0,len(G),2000):
        d=tri_dist(G[i:i+2000],A,B,C); dmin[i:i+2000]=d.min(1)
    return xs,zs,dmin.reshape(len(zs),len(xs)),int(keep.sum())
if __name__=='__main__':
    key=sys.argv[1]; yc=float(sys.argv[2]); r=float(sys.argv[3]); x0,x1,z0,z1=map(float,sys.argv[4:8])
    xs,zs,d,n=free_map(key,yc,r,x0,x1,z0,z1)
    print('tris used',n)
    for j in range(len(zs)-1,-1,-1):
        print('%6.3f '%zs[j]+''.join('.' if d[j,i]>=r else '#' for i in range(len(xs))))
    print('       x from %.3f step 0.004'%xs[0])
