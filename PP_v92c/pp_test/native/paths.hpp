// PP v84 -- turns the live predictor's per-tick frames into a few polylines for the in-game LineRenderers (pure C++, host-testable: tests/test_paths.cpp).
#pragma once
#include "live.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ppu {

struct P2 { double x = 0, z = 0; };
struct Path {
    int ball = -1;                 // host index of the ball (0..n-1)
    std::vector<P2> pts;           // thinned polyline, start .. end
    bool pocketed = false;         // the path ends in a pocket
    double tEnd = 0;               // time of the last point
};
struct Mark { int kind = 0; int ball = -1; P2 p; double t = 0; };   // kind 0 = first contact of the cue ball (ghost ball), 1 = pocket entry
struct Scene { std::vector<Path> paths; std::vector<Mark> marks; bool truncated = false; double t = 0; int ticks = 0; };

// Ramer-Douglas-Peucker (iterative, no recursion depth issues)
inline void rdp(const std::vector<P2>& in, double eps, std::vector<P2>& out) {
    out.clear(); const size_t n = in.size(); if (n <= 2) { out = in; return; }
    std::vector<char> keep(n, 0); keep[0] = keep[n - 1] = 1; std::vector<std::pair<size_t, size_t>> st; st.push_back({0, n - 1});
    while (!st.empty()) {
        auto [a, b] = st.back(); st.pop_back(); if (b <= a + 1) continue;
        const double ax = in[a].x, az = in[a].z, bx = in[b].x, bz = in[b].z, dx = bx - ax, dz = bz - az, L = std::sqrt(dx * dx + dz * dz);
        double best = -1; size_t bi = a;
        for (size_t i = a + 1; i < b; ++i) {
            double d; if (L < 1e-12) d = std::hypot(in[i].x - ax, in[i].z - az); else d = std::fabs(dz * (in[i].x - ax) - dx * (in[i].z - az)) / L;
            if (d > best) { best = d; bi = i; }
        }
        if (best > eps) { keep[bi] = 1; st.push_back({a, bi}); st.push_back({bi, b}); }
    }
    for (size_t i = 0; i < n; ++i) if (keep[i]) out.push_back(in[i]);
}

// minMove: a ball that never leaves a 6 mm circle around its start is "did not move" and gets no path.  eps: 2.5 mm line tolerance.
inline Scene buildScene(const pp2::live::Out& R, double minMove = 0.006, double eps = 0.0025) {
    Scene S; S.truncated = R.truncated; S.t = R.t; S.ticks = R.ticks; const int n = (int)R.ids.size();
    std::vector<double> pocketT(n, -1), pocketX(n, 0), pocketZ(n, 0);
    for (auto& e : R.events) {
        if (e.what == "pocket" && e.ball >= 0 && e.ball < n) { pocketT[e.ball] = e.t; pocketX[e.ball] = e.x; pocketZ[e.ball] = e.z; S.marks.push_back({1, e.ball, {e.x, e.z}, e.t}); }
        else if (e.what.compare(0, 13, "first contact") == 0) S.marks.push_back({0, e.ball, {e.x, e.z}, e.t});
    }
    for (int i = 0; i < n; ++i) {
        std::vector<P2> raw; double tLast = 0;
        for (auto& f : R.frames) {
            if (i >= (int)f.alive.size() || !f.alive[i]) break;   // left play (pocket): the frames after it hold the parked position
            if ((int)f.x.size() <= i) break;
            const P2 p{f.x[i], f.z[i]}; if (!raw.empty() && std::hypot(p.x - raw.back().x, p.z - raw.back().z) < 1e-5) { tLast = f.t; continue; } raw.push_back(p); tLast = f.t;
        }
        if (raw.empty()) continue;
        const bool pk = pocketT[i] >= 0;
        if (pk) { const P2 e{pocketX[i], pocketZ[i]}; if (std::hypot(e.x - raw.back().x, e.z - raw.back().z) > 1e-5) raw.push_back(e); tLast = pocketT[i]; }
        double far = 0; for (auto& p : raw) far = std::max(far, std::hypot(p.x - raw[0].x, p.z - raw[0].z));
        if (far < minMove && !pk) continue;
        Path P; P.ball = i; P.pocketed = pk; P.tEnd = tLast; rdp(raw, eps, P.pts); if (P.pts.size() < 2) continue; S.paths.push_back(std::move(P));
    }
    return S;
}

}  // namespace ppu
