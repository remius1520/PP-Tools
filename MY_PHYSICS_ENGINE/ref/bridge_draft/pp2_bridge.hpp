// v86: bridge from the NEW exact simulator (core/pp2/pp2_engine.hpp, Level 9-12 table mesh from the game's files) to the v85 drawing code.
// It converts the new engine's event log into pp::Result (tracks / finals) so payload.cpp keeps drawing exactly as before.
// Used only when it APPLIES (pockets + ball radius match the embedded table, no spin); otherwise payload.cpp falls back to the old pp::Sim.
#pragma once
#include "../core/predict_core.hpp"
#include "../core/pp2/pp2_engine.hpp"
#include "pp2_l11_data.inc"
#include <string>
#include <map>
#include <memory>

namespace ppb {

inline pp2::Sim& sim() {
    static pp2::Sim* S = nullptr;
    if (!S) { S = new pp2::Sim(); S->W.loadMem(PP2_L11_TRI, PP2_L11_MAT, PP2_L11_NT, PP2_L11_PK, PP2_L11_PKN, PP2_L11_NP); }
    return *S;
}

// Does the embedded table match the one the game is showing?  Every game pocket must sit on one of the 6 embedded pocket triggers (xz, 3.5 cm),
// and the ball radius must be the Level 9-12 radius (0.0275).  why = reason when not.
inline bool applies(const std::vector<pp::V2>& pk, double cr, std::string& why) {
    pp2::Sim& S = sim();
    if (pk.size() != 6) { why = "pocket count != 6"; return false; }
    if (std::fabs(cr - 0.0275) > 0.0006) { why = "ball radius != 0.0275"; return false; }
    int matched = 0;
    for (auto& q : pk) { bool ok = false; for (auto& p : S.W.P) { double cx = 0.5 * (p.lo.x + p.hi.x), cz = 0.5 * (p.lo.z + p.hi.z); if (std::hypot(q.x - cx, q.z - cz) < 0.035) { ok = true; break; } } if (ok) ++matched; }
    if (matched != 6) { why = "pockets do not match the embedded table"; return false; }
    return true;
}

struct Opts {
    int maxBounces = 6, maxChain = 8;
    double drag = 0.62, tmax = 25.0, cueY = -0.0296, R = 0.0275;
    bool firstContact = true;       // the game's server override of the first cue contact (only meaningful with the real drag / real power)
    double stop = 0.002;
};

// Ball ids: pp::BallIn.id (list index), cue = -1.  speed = initial cue speed (F).  Returns tracks + finals in the v85 format.
inline pp::Result run(const std::vector<pp::BallIn>& balls, pp::V2 cue, pp::V2 dir, double speed, const std::vector<pp::V2>& pk, const Opts& o, pp2::Prediction* predOut = nullptr) {
    using namespace pp;
    pp2::Sim& S = sim(); S.C = pp2::Cfg(); S.C.R = o.R; S.C.Y = o.cueY; S.C.drag = o.drag; S.C.tmax = o.tmax; S.C.stop = o.stop; S.C.firstContact = o.firstContact;
    std::vector<pp2::Ball> B; std::vector<int> ids; B.push_back({-1, cue.x, cue.z, 0, 0}); ids.push_back(-1);
    for (auto& b : balls) { B.push_back({b.id, b.p.x, b.p.z, 0, 0}); ids.push_back(b.id); }
    pp2::Shot sh; sh.cueId = -1; sh.dx = dir.x; sh.dz = dir.z; sh.F = speed;
    pp2::Result E = S.run(B, &sh, false, true);
    if (predOut) *predOut = E.pred;
    Result R; const size_t n = B.size();
    // embedded pocket k -> game pocket index (nearest centre)
    std::vector<int> pmap(S.W.P.size(), -1);
    for (size_t k = 0; k < S.W.P.size(); ++k) { double cx = 0.5 * (S.W.P[k].lo.x + S.W.P[k].hi.x), cz = 0.5 * (S.W.P[k].lo.z + S.W.P[k].hi.z), best = 1e9;
        for (size_t q = 0; q < pk.size(); ++q) { double d = std::hypot(pk[q].x - cx, pk[q].z - cz); if (d < best) { best = d; pmap[k] = (int)q; } } }
    std::vector<int> trk(n, -1), gen(n, -1), bnc(n, 0); std::vector<char> rec(n, 0); std::vector<double> tRest(n, 0.0);
    auto V = [](double x, double z) { return V2{x, z}; };
    auto openTrack = [&](size_t i, V2 at, double t, int g, int parent) {
        Track k; k.id = ids[i]; k.parent = parent; k.gen = g; k.t0 = t; k.t1 = t; bool rc = g <= o.maxChain; if (rc) k.pts.push_back(at);
        R.tracks.push_back(k); trk[i] = (int)R.tracks.size() - 1; gen[i] = g; bnc[i] = 0; rec[i] = rc;
    };
    auto closeTrack = [&](size_t i, V2 at, double t, End e, int hit, V2 ghost, int pocket) {
        if (trk[i] < 0) return; Track& k = R.tracks[trk[i]];
        if (rec[i]) { if (k.pts.empty() || len(k.pts.back() - at) > 1e-9) k.pts.push_back(at); }
        k.end = e; k.hitId = hit; k.ghost = ghost; k.pocket = pocket; k.t1 = t; trk[i] = -1; rec[i] = 0;
    };
    openTrack(0, cue, 0.0, 0, -2);                                   // the cue ball
    for (const pp2::TEv& e : E.tev) {
        const size_t a = (size_t)e.a;
        if (e.kind == 1) {                                           // solid / cushion
            if (trk[a] >= 0 && rec[a]) {
                Track& k = R.tracks[trk[a]]; k.pts.push_back(V(e.pa.x, e.pa.z)); k.bounceIdx.push_back((int)k.pts.size() - 1); ++bnc[a];
                if (bnc[a] >= o.maxBounces) { k.end = END_BOUNCELIMIT; k.t1 = e.t; rec[a] = 0; }
            }
        } else if (e.kind == 2) {                                    // ball x ball
            const size_t b = (size_t)e.b; V2 pa = V(e.pa.x, e.pa.z), pb = V(e.pb.x, e.pb.z);
            int g0 = std::max(trk[a] >= 0 || gen[a] >= 0 ? gen[a] : -1, trk[b] >= 0 || gen[b] >= 0 ? gen[b] : -1); int gn = g0 + 1;
            bool aMover = trk[a] >= 0 || !(trk[b] >= 0);             // the ball that was moving is the mover
            size_t m = aMover ? a : b, t = aMover ? b : a; V2 pm = aMover ? pa : pb, pt = aMover ? pb : pa; double sm = aMover ? e.sa : e.sb, st = aMover ? e.sb : e.sa;
            int mid = ids[m], tid = ids[t];
            closeTrack(m, pm, e.t, END_BALL, tid, pm, -1);
            if (trk[t] >= 0) closeTrack(t, pt, e.t, END_BALL, mid, pt, -1);
            openTrack(m, pm, e.t, gn, -1); if (sm <= o.stop) { R.tracks[trk[m]].end = END_REST; tRest[m] = e.t; trk[m] = -1; }
            openTrack(t, pt, e.t, gn, mid); if (st <= o.stop) { R.tracks[trk[t]].end = END_REST; tRest[t] = e.t; trk[t] = -1; }
        } else if (e.kind == 3) {                                    // pocket
            closeTrack(a, V(e.pa.x, e.pa.z), e.t, END_POCKET, -2, V2{}, e.pocket >= 0 && e.pocket < (int)pmap.size() ? pmap[e.pocket] : -1); tRest[a] = e.t;
        } else if (e.kind == 4) {                                    // came to rest
            closeTrack(a, V(e.pa.x, e.pa.z), e.t, END_REST, -2, V2{}, -1); tRest[a] = e.t;
        }
    }
    for (size_t i = 0; i < n; ++i) if (trk[i] >= 0) closeTrack(i, V(E.balls[i].x, E.balls[i].z), E.t, END_REST, -2, V2{}, -1);
    for (size_t i = 0; i < n; ++i) { int pi = -1; if (E.balls[i].pocketed) for (auto& t : R.tracks) if (t.id == ids[i] && t.end == END_POCKET) pi = t.pocket;
        R.finals.push_back({ids[i], V(E.balls[i].x, E.balls[i].z), E.balls[i].pocketed, pi, tRest[i]}); }
    return R;
}

}  // namespace ppb
