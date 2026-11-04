#!/usr/bin/env python3
"""P2S: xz convex hull of every pocket-trigger MeshCollider (tag 20002 PocketInside, isTrigger) in sharedassets0.assets, per table root.
Setup (paths as in level_geo.py): python3 tools/ufs.py data.unity3d /home/claude/u/out_data ; tools copied to /home/claude/u/tools ; run from /home/claude/u.
Output: /home/claude/u/pocket_hulls.json  (root -> [{name, mesh, verts, hull[[x,z]..], ymin, ymax, aabb[x0,z0,x1,z1], area_ratio}])"""
import sys, json
sys.path.insert(0, 'tools'); sys.argv = ['x']
import level_geo as L, scene as S
def hull(pts):
    pts = sorted(set(pts))
    cr = lambda o, a, b: (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])
    lo = []
    for p in pts:
        while len(lo) >= 2 and cr(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    up = []
    for p in reversed(pts):
        while len(up) >= 2 and cr(up[-2], up[-1], p) <= 0: up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]
area = lambda h: abs(sum(h[i][0]*h[(i+1) % len(h)][1] - h[(i+1) % len(h)][0]*h[i][1] for i in range(len(h)))) / 2
out = {}; skipped = []
for c in S.res['mesh']:
    if c['tag'] != 20002 or not c['trigger']: continue
    root = c['path'].split('/')[0]
    if c['mesh'] not in S.O: skipped.append((c['path'], c['mesh'])); continue
    name, pos, idx = L.mesh_geo(c['mesh'])
    wp = [L.xform(p, c['pos'], c['rot'], c['scale']) for p in pos]
    xz = [(round(p[0], 6), round(p[2], 6)) for p in wp]; h = hull(xz)
    xs = [p[0] for p in xz]; zs = [p[1] for p in xz]; ys = [p[1] for p in wp]
    out.setdefault(root, []).append(dict(name=c['path'].split('/', 1)[1], mesh=name, verts=len(pos), hull=h, ymin=min(ys), ymax=max(ys),
        aabb=[min(xs), min(zs), max(xs), max(zs)], area_ratio=area(h) / ((max(xs)-min(xs)) * (max(zs)-min(zs)))))
json.dump(out, open('pocket_hulls.json', 'w')); print('skipped (null mesh pptr):', skipped)
