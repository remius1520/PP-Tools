// PP2 P2C: runtime wiring of the mesh backend. Chooses WHICH of the 6 real tables is in the match and builds the MeshWorld for it.
// Inputs are runtime facts only: ball radius (Ball.radius), pocket positions (Pocket.orbitPoint), optional SphereCast calibration limits
// (ball-centre limits, as produced by pp::calibrateTable) and the ball centre height. The table is NEVER guessed: if two tables fit equally
// well the result is `ambiguous` and the caller must keep the legacy rectangle table (safe fallback).
#pragma once
#include "pp2_mesh.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <utility>

namespace pp2 { namespace sim {

struct TableSelect {
    const geo::TableGeo* g = nullptr;
    double score = 1e9, second = 1e9;       // lower = better; second = best score of any OTHER table that also passed the radius filter
    double rErr = 1e9, pocketErr = -1, calErr = -1;
    int candidates = 0;                     // tables that passed the radius filter
    bool ambiguous = true;                  // true -> do not use the mesh
    std::string why;
};

// file value of the rack height (ball centre height) per table (data/pp2_tables_summary.json); NaN = unknown from the files (Lvl1_4)
inline double fileRackY(const char* key) {
    struct E { const char* k; double y; };
    static const E T[] = {{"Lvl5_6", -0.0235}, {"Lvl7_8", -0.0239}, {"Lvl9", -0.0296}, {"Lvl9_12", -0.0296}, {"Lvl13", 0.0045}};
    for (auto& e : T) if (std::string(e.k) == key) return e.y;
    return std::nan("");
}

// pk: (PocketLocation 1..6, position xz). calLim: nullptr or {xmin,xmax,zmin,zmax} of the BALL CENTRE (cushion face -/+ r).
inline TableSelect selectTable(const std::vector<std::pair<int, V2>>& pk, double r, const double* calLim = nullptr, double radiusTol = 0.0004, double ambigMargin = 0.01) {
    TableSelect S; std::vector<std::pair<double, const geo::TableGeo*>> sc;
    for (auto& g : geo::tables()) {
        double re = std::fabs(r - geo::ballRadius(g)); if (re > radiusTol) continue;
        double cost = 0, pe = -1, ce = -1;
        if (pk.size() >= 4) {                                       // each observed pocket -> nearest UNUSED trigger centre of this table
            std::vector<bool> used(g.pockets.size(), false); double sum = 0;
            for (auto& q : pk) { int bi = -1; double bd = 1e9; for (size_t i = 0; i < g.pockets.size(); ++i) if (!used[i]) { double d = std::hypot(g.pockets[i].x - q.second.x, g.pockets[i].z - q.second.z); if (d < bd) { bd = d; bi = (int)i; } } if (bi >= 0) { used[bi] = true; sum += bd; } }
            pe = sum / (double)pk.size(); cost += 0.5 * pe;
        }
        if (calLim) {
            ce = (std::fabs(g.xl - (calLim[0] - r)) + std::fabs(g.xr - (calLim[1] + r)) + std::fabs(g.zb - (calLim[2] - r)) + std::fabs(g.zt - (calLim[3] + r))) / 4.0; cost += ce;
        }
        if (pe < 0 && ce < 0) cost = 1e3 * re;                      // radius is the only evidence
        sc.push_back({cost, &g}); if (!S.g || cost < S.score) { S.g = &g; S.score = cost; S.rErr = re; S.pocketErr = pe; S.calErr = ce; }
    }
    S.candidates = (int)sc.size();
    if (sc.empty()) { S.g = nullptr; S.why = "no table has this ball radius"; return S; }
    for (auto& e : sc) if (e.second != S.g) S.second = std::min(S.second, e.first);
    if (sc.size() == 1) { S.ambiguous = false; S.why = "unique radius match"; return S; }
    if (S.second - S.score < ambigMargin) { S.ambiguous = true; S.why = "tables with the same ball radius fit equally (pockets alone cannot separate them; need cushion calibration)"; return S; }
    S.ambiguous = false; S.why = "best of " + std::to_string(sc.size()) + " by pockets/calibration"; return S;
}

// Ready-to-use scene: `table.obs` points at `world`, so a Scene must stay where it was built (never copy it).
struct Scene {
    MeshWorld world; Table table; TableSelect sel; bool ok = false; double yc = 0, r = 0; std::string note;
    Scene() {}
    Scene(const Scene&) = delete; Scene& operator=(const Scene&) = delete;
    // yc: ball centre height read at runtime (ball.transform.position.y). Returns false (and keeps ok=false) when the mesh must not be used.
    bool build(const std::vector<std::pair<int, V2>>& pk, double radius, double ballY, const double* calLim = nullptr) {
        ok = false; note.clear(); sel = selectTable(pk, radius, calLim);
        if (!sel.g || sel.ambiguous) { note = "mesh not used: " + sel.why; return false; }
        const geo::MeshSrc* s = geo::meshSource(sel.g->key); if (!s) { note = "mesh not used: no mesh data for " + std::string(sel.g->key); return false; }
        double fy = fileRackY(sel.g->key);
        if (!(std::fabs(ballY) < 5.0)) { note = "mesh not used: ball height implausible"; return false; }
        char b[160]; if (!std::isnan(fy) && std::fabs(ballY - fy) > 0.02) { snprintf(b, sizeof b, "WARNING ball y %.4f differs from file rackY %.4f by >2 cm (runtime value used)", ballY, fy); note = b; }
        yc = ballY; r = radius; world.build(*s, *sel.g, yc, r);
        table = geo::toSimTable(*sel.g, r); table.obs = &world; ok = !world.tris.empty();
        if (!ok) note = "mesh not used: no triangles at ball height";
        return ok;
    }
};

}}
