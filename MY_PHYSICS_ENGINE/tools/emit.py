import sys, json, os
from gen_tables import *
import pocket_hulls_lib as H
OUT=sys.argv[1] if len(sys.argv)>1 else 'out'
T={'lvl1_4':'TableLvl1-4Model','lvl5_6':'TableLvl5&6','lvl7_8':'TableLvl7-8','lvl9':'Table-GameLevel9','level11':'Table-GameLevel9-12Model','lvl13':'Level13Table'}
def nm(c,used):
    n=c['path'].split('/')[-1].replace(' ','_').replace('(','').replace(')','')
    base=n; i=2
    while n in used: n=f'{base}_{i}'; i+=1
    used.add(n); return n
def emit(key,root):
    cs=colliders(root); os.makedirs(OUT,exist_ok=True)
    sol=[];pk=[];hl=[];used=set()
    for c in cs:
        if not c['active']: continue
        if c['trigger']:
            if c['tag']!=20002: continue
            name,pos,idx=L.mesh_geo(c['mesh']); wp=[L.xform(p,c['pos'],c['rot'],c['scale']) for p in pos]
            n=nm(c,used); xs=[p[0] for p in wp]; ys=[p[1] for p in wp]; zs=[p[2] for p in wp]
            pk.append((n,min(xs),min(ys),min(zs),max(xs),max(ys),max(zs)))
            h=H.hull([(round(p[0],6),round(p[2],6)) for p in wp]); hl.append((n,h))
            continue
        m=letter(c)
        for t in tris(c): sol.append((m,c['layer'],[x for p in t for x in p]))
    for b in S.res['box']:
        if not b['path'].startswith(root+'/') or b['trigger']: continue
        go=struct.unpack_from('<iq',S.blob(b['id']),0)[1]
        if not (in_hierarchy(go) and b['enabled']): continue
        hx,hy,hz=[s/2 for s in b['size']]; cx,cy,cz=b['center']
        C=[[L.xform((cx+sx*hx,cy+sy*hy,cz+sz*hz),b['pos'],b['rot'],b['scale']) for sz in(-1,1)] for sx in(-1,1) for sy in(-1,1)]
        V=lambda i,j,k:C[i*2+j][k]
        quads=[((0,0,0),(0,1,0),(0,1,1),(0,0,1)),((1,0,0),(1,0,1),(1,1,1),(1,1,0)),((0,0,0),(0,0,1),(1,0,1),(1,0,0)),((0,1,0),(1,1,0),(1,1,1),(0,1,1)),((0,0,0),(1,0,0),(1,1,0),(0,1,0)),((0,0,1),(0,1,1),(1,1,1),(1,0,1))]
        for q in quads:
            p=[V(*i) for i in q]
            for t in ((p[0],p[1],p[2]),(p[0],p[2],p[3])): sol.append((letter(b),b['layer'],[x for pp in t for x in pp]))
    with open(f'{OUT}/{key}_solids.txt','w') as f:
        f.write('# mat(E=tag Edge + 0.92 material, K=other 0.92 material, D=default) layer  x1 y1 z1 x2 y2 z2 x3 y3 z3   (world, from game files; active-in-hierarchy colliders only)\n')
        for m,ly,v in sol: f.write(f'{m} {ly} '+' '.join(f'{x:.8g}' for x in v)+'\n')
    with open(f'{OUT}/{key}_pockets.txt','w') as f:
        f.write('# name minx miny minz maxx maxy maxz  (CheckBall trigger AABB, world)\n')
        for p in pk: f.write(p[0]+' '+' '.join(f'{x:.6f}' for x in p[1:])+'\n')
    with open(f'{OUT}/{key}_pocket_hulls.txt','w') as f:
        f.write('# name n x0 z0 x1 z1 ...  (xz convex hull of each pocket trigger MeshCollider, world, CCW)\n')
        for n,h in hl: f.write(f'{n} {len(h)} '+' '.join(f'{x:.6f} {z:.6f}' for x,z in h)+'\n')
    from collections import Counter
    print(key,root,'tris',len(sol),Counter(m for m,_,_ in sol),'pockets',len(pk))
for k,r in T.items(): emit(k,r)
