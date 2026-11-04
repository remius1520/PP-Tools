// PP2 P2B: mesh-collision backend. A ball (sphere, radius r, centre height yc, Y frozen by the Rigidbody constraint) is swept exactly against the
// game's own collider triangles (read from data.unity3d: Edge rails, PocketInside baskets, Corners-Table, Poket-Down; see data/pp2_tables.json).
//
// Geometry: the sphere centre moves in the plane y = yc. A contact happens when the 3-D distance centre -> triangle reaches r. The inflated
// triangle is (prism over the face) U (3 edge cylinders) U (3 vertex spheres); the first entry time is the minimum over those primitives
// (exact, no sampling, no tunnelling). The contact normal n3 is the 3-D normal at the touching point.
//
// Response (PhysX with the ball's Y frozen): the contact impulse is along n3 but the ball can only move in xz, so the effective inverse mass along
// n3 is |n_xz|^2/m. That makes the xz velocity change  dv = -(1+e) (v.nh) nh  with nh = n_xz/|n_xz|, i.e. an ordinary reflection about the PROJECTED
// normal, restitution e (e is applied to v.n3 = |n_xz| (v.nh), which scales out). Friction is 0 for every ball material (min-combine with the ball's 0).
// e per triangle = combined material bounciness: Edges_Physics / Pocket_Physics 0.92, no material (Corners-Table, Poket-Down) = PhysX default 0 (Minimum).
#pragma once
#include "pp2_sim.hpp"
#include "pp2_tablegeo.hpp"
#include "pp2_meshdata.hpp"
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace pp2 { namespace sim {

struct V3 { double x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross3(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len3(V3 a) { return std::sqrt(dot3(a, a)); }

struct MeshWorld : Obstacle {
    struct Tri { V3 a, b, c, n; uint8_t tag, mat; };
    struct Zone { std::vector<Tri> tris; double x0, x1, z0, z1; };   // convex PocketInside trigger (OnTriggerEnter fires when the ball's SphereCollider overlaps it)
    double yc = 0, r = 0.03, cell = 0.04;
    double eMat[3] = {0.0, 0.92, 0.92};       // restitution per material code (0 none, 1 Edges_Physics, 2 Pocket_Physics)
    std::vector<Tri> tris;
    std::vector<Zone> zones;
    double gx0 = 0, gz0 = 0; int gnx = 0, gnz = 0;
    std::vector<std::vector<int>> grid;
    mutable std::vector<uint32_t> stamp; mutable uint32_t cur = 0;   // not thread-safe

    // yc: ball centre height (runtime fact: ball.transform.position.y; rackY of the rack transform is the file value), r: ball radius.
    void build(const geo::MeshSrc& s, const geo::TableGeo& g, double yc_, double r_) {
        yc = yc_; r = r_; tris.clear(); zones.clear();
        const double pad = 1e-6;
        for (int i = 0; i < s.nt; ++i) {
            V3 p[3]; for (int k = 0; k < 3; ++k) { int v = s.t[3 * i + k]; p[k] = {s.v[3 * v], s.v[3 * v + 1], s.v[3 * v + 2]}; }
            double lo = std::min(p[0].y, std::min(p[1].y, p[2].y)), hi = std::max(p[0].y, std::max(p[1].y, p[2].y));
            if (hi < yc - r - pad || lo > yc + r + pad) continue;                        // the sphere at height yc can never touch it
            V3 n = cross3(p[1] - p[0], p[2] - p[0]); double nl = len3(n); if (nl < 1e-12) continue; n = n * (1.0 / nl);
            tris.push_back({p[0], p[1], p[2], n, s.tag[i], s.mat[i]});
        }
        for (int z = 0; z < s.nz; ++z) {
            Zone Z; Z.x0 = Z.z0 = 1e9; Z.x1 = Z.z1 = -1e9;
            for (int i = s.zs[z]; i < s.zs[z + 1]; ++i) {
                V3 p[3]; for (int k = 0; k < 3; ++k) { int v = s.zt[3 * i + k]; p[k] = {s.zv[3 * v], s.zv[3 * v + 1], s.zv[3 * v + 2]}; Z.x0 = std::min(Z.x0, p[k].x); Z.x1 = std::max(Z.x1, p[k].x); Z.z0 = std::min(Z.z0, p[k].z); Z.z1 = std::max(Z.z1, p[k].z); }
                V3 n = cross3(p[1] - p[0], p[2] - p[0]); double nl = len3(n); if (nl < 1e-12) continue;
                Z.tris.push_back({p[0], p[1], p[2], n * (1.0 / nl), 0, 0});
            }
            zones.push_back(std::move(Z));
        }
        (void)g;
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (auto& t : tris) for (const V3* q : {&t.a, &t.b, &t.c}) { x0 = std::min(x0, q->x); x1 = std::max(x1, q->x); z0 = std::min(z0, q->z); z1 = std::max(z1, q->z); }
        if (tris.empty()) { grid.clear(); return; }
        gx0 = x0 - r - 0.01; gz0 = z0 - r - 0.01; gnx = (int)((x1 - x0 + 2 * r + 0.02) / cell) + 1; gnz = (int)((z1 - z0 + 2 * r + 0.02) / cell) + 1;
        grid.assign((size_t)gnx * gnz, {});
        for (size_t i = 0; i < tris.size(); ++i) {
            const Tri& t = tris[i];
            double a0 = std::min(t.a.x, std::min(t.b.x, t.c.x)) - r, a1 = std::max(t.a.x, std::max(t.b.x, t.c.x)) + r;
            double b0 = std::min(t.a.z, std::min(t.b.z, t.c.z)) - r, b1 = std::max(t.a.z, std::max(t.b.z, t.c.z)) + r;
            int i0 = clampi((int)((a0 - gx0) / cell), gnx), i1 = clampi((int)((a1 - gx0) / cell), gnx), j0 = clampi((int)((b0 - gz0) / cell), gnz), j1 = clampi((int)((b1 - gz0) / cell), gnz);
            for (int j = j0; j <= j1; ++j) for (int k = i0; k <= i1; ++k) grid[(size_t)j * gnx + k].push_back((int)i);
        }
        stamp.assign(tris.size(), 0); cur = 0;
    }
    static int clampi(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }

    // ---- exact sweep of a point (the ball centre) against ONE triangle inflated by r. Motion D = (dx,0,dz), t in [0,1]. ----
    static bool inTri(const Tri& T, V3 q) {
        V3 v0 = T.b - T.a, v1 = T.c - T.a, v2 = q - T.a;
        double d00 = dot3(v0, v0), d01 = dot3(v0, v1), d11 = dot3(v1, v1), d20 = dot3(v2, v0), d21 = dot3(v2, v1);
        double den = d00 * d11 - d01 * d01; if (std::fabs(den) < 1e-18) return false;
        double v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den, u = 1.0 - v - w; const double e = -1e-9;
        return u >= e && v >= e && w >= e;
    }
    struct Cand { double t = 2.0; V3 n; };
    static void edgeCyl(V3 P0, V3 D, double r, V3 A, V3 B, Cand& best) {
        V3 E = B - A; double L = len3(E); if (L < 1e-12) return; V3 e = E * (1.0 / L);
        V3 M = P0 - A; V3 Mp = M - e * dot3(M, e), Dp = D - e * dot3(D, e);
        double a = dot3(Dp, Dp), b = dot3(Mp, Dp), c = dot3(Mp, Mp) - r * r;
        if (a < 1e-18) return;
        double t;
        if (c <= 0) { if (b >= 0) return; t = 0; }
        else { if (b >= 0) return; double disc = b * b - a * c; if (disc < 0) return; t = (-b - std::sqrt(disc)) / a; }
        if (t < 0 || t > 1 || t >= best.t) return;
        V3 H = M + D * t; double u = dot3(H, e); if (u < 0 || u > L) return;
        V3 hp = H - e * u; double hl = len3(hp); if (hl < 1e-12) return;
        best.t = t; best.n = hp * (1.0 / hl);
    }
    static void vertSph(V3 P0, V3 D, double r, V3 Vt, Cand& best) {
        V3 M = P0 - Vt; double a = dot3(D, D), b = dot3(M, D), c = dot3(M, M) - r * r; if (a < 1e-18) return;
        double t;
        if (c <= 0) { if (b >= 0) return; t = 0; }
        else { if (b >= 0) return; double disc = b * b - a * c; if (disc < 0) return; t = (-b - std::sqrt(disc)) / a; }
        if (t < 0 || t > 1 || t >= best.t) return;
        V3 H = M + D * t; double hl = len3(H); if (hl < 1e-12) return;
        best.t = t; best.n = H * (1.0 / hl);
    }
    void sweepTri(const Tri& T, V3 P0, V3 D, Cand& best) const {
        double s0 = dot3(T.n, P0 - T.a), dn = dot3(T.n, D);
        if (s0 > r) { if (dn < 0) { double t = (r - s0) / dn; if (t <= 1 && t < best.t) { V3 H = P0 + D * t; if (inTri(T, H - T.n * r)) { best.t = std::max(0.0, t); best.n = T.n; } } } }
        else if (s0 < -r) { if (dn > 0) { double t = (-r - s0) / dn; if (t <= 1 && t < best.t) { V3 H = P0 + D * t; if (inTri(T, H + T.n * r)) { best.t = std::max(0.0, t); best.n = T.n * -1.0; } } } }
        else { V3 Q = P0 - T.n * s0; if (inTri(T, Q)) { if (s0 >= 0 ? dn < 0 : dn > 0) { if (0.0 < best.t) { best.t = 0; best.n = s0 >= 0 ? T.n : T.n * -1.0; } } } }
        edgeCyl(P0, D, r, T.a, T.b, best); edgeCyl(P0, D, r, T.b, T.c, best); edgeCyl(P0, D, r, T.c, T.a, best);
        vertSph(P0, D, r, T.a, best); vertSph(P0, D, r, T.b, best); vertSph(P0, D, r, T.c, best);
    }

    bool sweep(V2 p, V2 d, double rr, Contact& out) const override {
        (void)rr;                                                   // radius is fixed at build() (the grid is inflated by it)
        if (tris.empty()) return false;
        double x0 = std::min(p.x, p.x + d.x), x1 = std::max(p.x, p.x + d.x), z0 = std::min(p.z, p.z + d.z), z1 = std::max(p.z, p.z + d.z);
        int i0 = (int)std::floor((x0 - gx0) / cell), i1 = (int)std::floor((x1 - gx0) / cell), j0 = (int)std::floor((z0 - gz0) / cell), j1 = (int)std::floor((z1 - gz0) / cell);
        if (i1 < 0 || j1 < 0 || i0 >= gnx || j0 >= gnz) return false;
        i0 = clampi(i0, gnx); i1 = clampi(i1, gnx); j0 = clampi(j0, gnz); j1 = clampi(j1, gnz);
        V3 P0{p.x, yc, p.z}, D{d.x, 0, d.z}; Cand best; int bi = -1; ++cur;
        for (int j = j0; j <= j1; ++j) for (int k = i0; k <= i1; ++k) for (int idx : grid[(size_t)j * gnx + k]) {
            if (stamp[idx] == cur) continue;
            stamp[idx] = cur;
            Cand c = best; sweepTri(tris[idx], P0, D, c);
            if (c.t < best.t) { best = c; bi = idx; }
        }
        if (bi < 0 || best.t > 1.0) return false;
        double nx = best.n.x, nz = best.n.z, nl = std::sqrt(nx * nx + nz * nz);
        if (nl < 1e-6) return false;                                // contact normal is vertical: cannot change the xz velocity
        V2 nh{nx / nl, nz / nl};
        if (dot(nh, d) >= 0) return false;                          // not closing in the plane
        out.t = best.t; out.n = nh; out.e = eMat[tris[bi].mat]; out.tag = tris[bi].tag; out.tri = bi; return true;
    }
    // pocket capture = Unity trigger rule (decoded: Ball.OnTriggerEnter -> OnTriggerOccur -> TryGetComponent<Icollision> -> OnCollisionOccuer): the ball's SphereCollider
    // (centre at height yc, radius r) starts to overlap the convex PocketInside trigger mesh. Exact: inside-the-volume test + swept sphere vs the mesh triangles.
    static double sideTest(const Zone& Z, V3 P, bool& allNeg, bool& allPos) {
        allNeg = allPos = true; for (auto& t : Z.tris) { double s0 = dot3(t.n, P - t.a); if (s0 > 1e-12) allNeg = false; if (s0 < -1e-12) allPos = false; } return 0; }
    bool zone(V2 p, V2 d, double rr, double& tOut, int& idx) const override {
        (void)rr; V3 P0{p.x, yc, p.z}, D{d.x, 0, d.z}; double bestT = 2.0; int bi = -1;
        for (size_t i = 0; i < zones.size(); ++i) {
            const Zone& Z = zones[i];
            if (std::max(p.x, p.x + d.x) < Z.x0 - r || std::min(p.x, p.x + d.x) > Z.x1 + r || std::max(p.z, p.z + d.z) < Z.z0 - r || std::min(p.z, p.z + d.z) > Z.z1 + r) continue;
            bool neg, pos; sideTest(Z, P0, neg, pos);
            double t = 2.0;
            if (neg || pos) t = 0.0;                                 // centre already inside the convex volume
            else {
                Cand best; for (auto& T : Z.tris) { if (best.t <= 0) break; sweepTri(T, P0, D, best); }
                // sweepTri only reports closing contacts; a start already within r of the surface counts as overlap
                double m = 1e9; for (auto& T : Z.tris) { V3 q = closestOnTri(P0, T); m = std::min(m, len3(P0 - q)); }
                t = (m <= r) ? 0.0 : best.t;
            }
            if (t <= 1.0 && t < bestT) { bestT = t; bi = (int)i; }
        }
        if (bi < 0) return false;
        tOut = bestT; idx = bi; return true;
    }
    static V3 closestOnTri(V3 p, const Tri& T) {
        V3 a = T.a, b = T.b, c = T.c, ab = b - a, ac = c - a, ap = p - a; double d1 = dot3(ab, ap), d2 = dot3(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
        V3 bp = p - b; double d3 = dot3(ab, bp), d4 = dot3(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
        double vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
        V3 cp = p - c; double d5 = dot3(ab, cp), d6 = dot3(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
        double vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
        double va = d3 * d6 - d5 * d4; if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        double den = 1.0 / (va + vb + vc); return a + ab * (vb * den) + ac * (vc * den);
    }
};

}}   // namespace pp2::sim
