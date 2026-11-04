import sys, struct, json, io, contextlib
sys.argv=['x']; sys.path.insert(0,'tools')
with contextlib.redirect_stdout(io.StringIO()):
    import scene as S, level_geo as L
MAT={304:'E',305:'K'}
EDGE_TAG=20000; BOUNCY=(303,304,305)   # PhysicsMaterials with bounciness 0.92 (P2W-merge)
def letter(c):
    # 'E' = tag Edge (20000) + 0.92 material -- the engine treats 'E' as the Edge tag (cushion logic + rails AABB); an UNTAGGED collider with an edge material
    # (Level13 'Border' boxes: mat 304, layer 0, tag 0) is 'K' (0.92, no Edge logic).  'D' = default / Table_Physics.
    if c.get('tag') == EDGE_TAG and c['material'] in (303, 304): return 'E'
    return 'K' if c['material'] in BOUNCY else 'D'
def go_active(pid):
    b=S.blob(pid); n,=struct.unpack_from('<i',b,0); q=4+12*n+4
    ln,=struct.unpack_from('<I',b,q); q+=4; q=(q+ln+3)&~3
    return b[q+2]!=0
def in_hierarchy(go):
    while go in S.GO:
        if not go_active(go): return False
        tp=S.GO2TR.get(go)
        if tp is None: return False
        f=S.TR[tp]['father']
        if f not in S.TR: return True
        go=S.TR[f]['go']
    return True
def colliders(root):
    out=[]
    for c in S.res['mesh']:
        if not c['path'].startswith(root+'/') or c['mesh'] not in S.O: continue
        go=struct.unpack_from('<iq',S.blob(c['id']),0)[1]
        c=dict(c); c['active']=in_hierarchy(go) and bool(c['enabled'])
        out.append(c)
    return out
def tris(c):
    name,pos,idx=L.mesh_geo(c['mesh'])
    wp=[L.xform(p,c['pos'],c['rot'],c['scale']) for p in pos]
    return [[wp[idx[i+k]] for k in range(3)] for i in range(0,len(idx),3)]
if __name__=='__main__':
    from collections import Counter
    def k(v,n=3): return tuple(sorted(tuple(round(v[i*3+j],n)+0.0 for j in range(3)) for i in range(3)))
    ref=Counter()
    for l in open('/home/claude/w/pp2_sim/data/level11_solids.txt'):
        if l[0]=='#': continue
        f=l.split(); ref[(f[0],int(f[1]),k([float(x) for x in f[2:]]))]+=1
    mine=Counter()
    for c in colliders('Table-GameLevel9-12Model'):
        if c['trigger'] or not c['active']: continue
        for t in tris(c): mine[(letter(c),c['layer'],k([x for p in t for x in p]))]+=1
    print(sum(ref.values()),sum(mine.values()),'only ref',sum((ref-mine).values()),'only mine',sum((mine-ref).values()))
