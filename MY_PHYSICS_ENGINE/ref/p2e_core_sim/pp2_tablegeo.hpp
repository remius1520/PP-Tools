// PP2 real table geometry (phase P2A): cushion faces, rail ends and pocket triggers read from the game's own scene
// (data.unity3d -> sharedassets0.assets: Transform chain x Edge MeshColliders). Data in pp2_tablegeo_data.hpp (GENERATED).
// Which table is active in a match is a runtime fact (level) - pick by key or by matching measured rail positions (nearest()).
#pragma once
#include "pp2_sim.hpp"
#include "pp2_tablegeo_data.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace pp2 { namespace geo {

inline const TableGeo* find(const char* key) { for (auto& t : tables()) if (!std::strcmp(t.key, key)) return &t; return nullptr; }
inline double ballRadius(const TableGeo& g) { return 0.5 * g.ballScale; }   // SphereCollider.radius 0.5 (file) x lossyScale

// Table whose cushion faces are nearest to the measured ones (xl,xr,zb,zt) - for choosing the geometry at runtime from a SphereCast/raycast measurement.
inline const TableGeo* nearest(double xl, double xr, double zb, double zt, double* err = nullptr) {
    const TableGeo* best = nullptr; double be = 1e18;
    for (auto& t : tables()) { double e = std::fabs(t.xl - xl) + std::fabs(t.xr - xr) + std::fabs(t.zb - zb) + std::fabs(t.zt - zt); if (e < be) { be = e; best = &t; } }
    if (err) *err = be;
    return best;
}

// Simulator table: ball-CENTRE limits = face -/+ r. Pocket openings come from the real rail ends. `shrink` = how much of the ball radius is
// taken off the opening (1 = a ball passes the rail end only when its centre clears it by r; 0 = centre-line test). This is the 2-D
// approximation of a sphere vs box-corner contact until a mesh-collision backend exists (see HANDOFF_P2A).
inline sim::Table toSimTable(const TableGeo& g, double r, double shrink = 1.0) {
    sim::Table t; t.xmin = g.xl + r; t.xmax = g.xr - r; t.zmin = g.zb + r; t.zmax = g.zt - r;
    const double xc = 0.5 * (g.xl + g.xr), zc = 0.5 * (g.zb + g.zt), W = g.xr - g.xl;
    auto clampPos = [](double v) { return v < 0 ? 0.0 : v; };
    for (auto& P : g.pockets) {
        sim::Pocket k; k.p = {P.x, P.z}; k.mouth = 0.06;
        const bool top = P.z > zc; const std::vector<Seg>& zw = top ? g.topX : g.botX;
        const bool mid = std::fabs(P.x - xc) < 0.25 * W;
        if (mid) {
            // opening between the two innermost rail pieces of this wall
            double a = zw.front().b, b = zw.back().a;
            for (size_t i = 0; i + 1 < zw.size(); ++i) if (zw[i + 1].a - zw[i].b > b - a || i == 0) { a = zw[i].b; b = zw[i + 1].a; }
            k.p.x = 0.5 * (a + b); k.gap = clampPos(0.5 * (b - a) - shrink * r); k.gapAlongX = k.gapAlongZ = k.gap;
        } else {
            const bool right = P.x > xc;
            double xEnd = right ? zw.back().b : zw.front().a;                 // outer end of the long rail
            const std::vector<Seg>& xw = right ? g.rightZ : g.leftZ;
            double zEnd = top ? xw.back().b : xw.front().a;                   // end of the short rail towards this corner
            for (auto& s : xw) { if (top) zEnd = std::max(zEnd, s.b); else zEnd = std::min(zEnd, s.a); }
            k.gapAlongX = clampPos(std::fabs(P.x - xEnd) - shrink * r);
            k.gapAlongZ = clampPos(std::fabs(P.z - zEnd) - shrink * r);
            k.gap = k.gapAlongX;
        }
        t.pockets.push_back(k);
    }
    return t;
}

}}
