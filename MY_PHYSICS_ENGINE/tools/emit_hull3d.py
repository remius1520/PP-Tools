#!/usr/bin/env python3
"""P2X: exact 3D convex hull (triangles) of every pocket-trigger MeshCollider (tag 20002, isTrigger, convex) per table, world space.
Same pocket names/order as emit.py.  Output: <OUT>/<key>_pocket_hull3d.txt  lines: name ntri x1 y1 z1 x2 y2 z2 x3 y3 z3 ... (outward-wound).
Setup as emit.py (cwd /home/claude/u, tools linked at ./tools).  usage: python3 tools/emit_hull3d.py <outdir>"""
import sys, os
_ARG = list(sys.argv)   # gen_tables resets sys.argv on import
import numpy as np
from scipy.spatial import ConvexHull
from gen_tables import *
OUT = _ARG[1] if len(_ARG) > 1 else 'out'
T = {'lvl1_4': 'TableLvl1-4Model', 'lvl5_6': 'TableLvl5&6', 'lvl7_8': 'TableLvl7-8', 'lvl9': 'Table-GameLevel9', 'level11': 'Table-GameLevel9-12Model', 'lvl13': 'Level13Table'}
def nm(c, used):
    n = c['path'].split('/')[-1].replace(' ', '_').replace('(', '').replace(')', ''); base = n; i = 2
    while n in used: n = f'{base}_{i}'; i += 1
    used.add(n); return n
os.makedirs(OUT, exist_ok=True)
for key, root in T.items():
    used = set(); lines = []
    for c in colliders(root):
        if not c['active'] or not c['trigger'] or c['tag'] != 20002: continue
        name, pos, idx = L.mesh_geo(c['mesh']); wp = np.array([L.xform(p, c['pos'], c['rot'], c['scale']) for p in pos], dtype=float)
        h = ConvexHull(wp); cen = wp[h.vertices].mean(axis=0); tri = []
        for s in h.simplices:
            a, b, cc = wp[s]; n = np.cross(b - a, cc - a)
            if np.dot(n, a - cen) < 0: b, cc = cc, b          # outward winding
            tri.append((a, b, cc))
        lines.append(nm(c, used) + f' {len(tri)} ' + ' '.join('%.7g' % x for t in tri for p in t for x in p))
        print(key, lines[-1].split()[0], 'hull verts', len(h.vertices), 'tris', len(tri), 'vol %.3e' % h.volume)
    open(f'{OUT}/{key}_pocket_hull3d.txt', 'w').write('# name ntri x1 y1 z1 x2 y2 z2 x3 y3 z3 ...  (3D convex hull of each pocket trigger MeshCollider, world, outward winding; scipy ConvexHull of the mesh vertices)\n' + '\n'.join(lines) + '\n')
