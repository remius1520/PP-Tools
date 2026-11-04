import sys, struct, json
sys.path.insert(0,'/home/claude/u/tools')
import sf, mesh, scene as S
resS=open('/home/claude/u/out_data/sharedassets0.assets.resS','rb').read()
def mesh_geo(pid):
    _,off,size,cid=S.O[pid]; b=S.d[off:off+size]
    m=mesh.parse_mesh(b)
    pth=b'sharedassets0.assets.resS'; j=b.find(pth)
    o,=struct.unpack_from('<Q',b,j-16); sz,=struct.unpack_from('<I',b,j-8)
    m['vd']=resS[o:o+sz]
    pos,idx=mesh.geometry(m)
    return m['name'],pos,idx
def xform(p,pos,rot,sc):
    v=(p[0]*sc[0],p[1]*sc[1],p[2]*sc[2]); r=S.qrot(rot,v); return tuple(r[i]+pos[i] for i in range(3))
if __name__=='__main__':
    root=sys.argv[1]; out={'root':root,'colliders':[]}
    allx=[];allz=[]
    for c in S.res['mesh']+[]:
        if not c['path'].startswith(root): continue
        name,pos,idx=mesh_geo(c['mesh'])
        wp=[xform(p,c['pos'],c['rot'],c['scale']) for p in pos]
        tris=[[list(map(lambda x:round(x,6),wp[idx[i+k]])) for k in range(3)] for i in range(0,len(idx),3)]
        out['colliders'].append(dict(path=c['path'],material=c['material'],trigger=c['trigger'],convex=c['convex'],layer=c['layer'],tag=c['tag'],tris=tris))
        if c['layer']==11:
            for p in wp: allx.append(p[0]); allz.append(p[2])
            ys=[p[1] for p in wp]
            print(c['path'],'verts',len(pos),'tris',len(tris),'X',round(min(p[0] for p in wp),4),round(max(p[0] for p in wp),4),'Z',round(min(p[2] for p in wp),4),round(max(p[2] for p in wp),4),'Y',round(min(ys),4),round(max(ys),4))
    json.dump(out,open('/home/claude/u/level_%s.json'%root.replace('/','_'),'w'))
    print('colliders',len(out['colliders']),'triangles',sum(len(c['tris']) for c in out['colliders']))
