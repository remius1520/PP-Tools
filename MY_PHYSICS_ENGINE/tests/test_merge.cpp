// V3_2 merge (P2Z2 + V3_2): (1) Result::tev event log (run(..., wantEv=true)) is purely additive: same final state as without it,
// events are time-ordered and consistent with wallHits/ballHits/pocket events; (2) Sim::detect picks the right table from its pocket centres.
#include "../src/pp2_engine.hpp"
#include <cstdio>
using namespace pp2;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static bool same(const Result& a, const Result& b) {
    if (a.balls.size() != b.balls.size() || a.t != b.t || a.wallHits != b.wallHits || a.ballHits != b.ballHits) return false;
    for (size_t i = 0; i < a.balls.size(); ++i) if (a.balls[i].x != b.balls[i].x || a.balls[i].z != b.balls[i].z || a.balls[i].pocketed != b.balls[i].pocketed) return false;
    return true;
}
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "data";
    for (const auto& t : kTables) {
        Sim S; if (!S.loadTable(dir, t.key)) { printf("FAIL %s load\n", t.key); ++fails; continue; }
        // ---- event log on a break-like shot: cue hits a ball, balls bounce, some stop
        double cx = 0, cz = -0.2; std::vector<Ball> B{{0, cx - 0.5, cz, 0, 0}, {1, cx + 0.05, cz, 0, 0}, {2, cx + 0.05 + 2.2 * t.R, cz + 0.02, 0, 0}, {3, cx + 0.3, cz - 0.15, 0, 0}};
        Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0; sh.F = 2.5; sh.spin = -1;
        Result r0 = S.run(B, &sh, false, false), r1 = S.run(B, &sh, false, true), r2 = S.run(B, &sh, true, true);
        CHECK(same(r0, r1) && same(r0, r2), "%s: wantEv changed the simulation", t.key);
        CHECK(r0.tev.empty() && !r1.tev.empty(), "%s: tev only when asked (off=%zu on=%zu)", t.key, r0.tev.size(), r1.tev.size());
        int nW = 0, nB = 0, nP = 0, nS = 0; bool ordered = true, idsOk = true, first = true; double last = -1;
        for (auto& e : r1.tev) {
            if (e.t + 1e-12 < last) ordered = false; last = e.t;
            if (e.a < 0 || e.a >= (int)B.size() || (e.kind == 2 && (e.b < 0 || e.b >= (int)B.size()))) idsOk = false;
            nW += e.kind == 1; nB += e.kind == 2; nP += e.kind == 3; nS += e.kind == 4; (void)first;
        }
        CHECK(ordered && idsOk, "%s: tev ordering/ids", t.key);
        CHECK(nW == r1.wallHits && nB == r1.ballHits, "%s: tev counts W=%d/%d B=%d/%d", t.key, nW, r1.wallHits, nB, r1.ballHits);
        CHECK(nB >= 1, "%s: expected at least one ball-ball event", t.key);
        int pocketedBalls = 0; for (auto& b : r1.balls) pocketedBalls += b.pocketed; CHECK(nP == pocketedBalls, "%s: pocket events %d vs pocketed %d", t.key, nP, pocketedBalls);
        int inBalls = 0; for (auto& b : r1.balls) inBalls += b.in; CHECK(nS <= inBalls && nS >= 1, "%s: rest events %d (balls on table %d)", t.key, nS, inBalls);
        // pocket event: a ball shot at a side pocket
        { double minx = 1e9, maxx = -1e9, minz = 1e9, maxz = -1e9; for (auto& rl : S.W.rails) { minx = std::min(minx, rl.lo.x); maxx = std::max(maxx, rl.hi.x); minz = std::min(minz, rl.lo.z); maxz = std::max(maxz, rl.hi.z); }
          double mx = 0.5 * (minx + maxx), mz = 0.5 * (minz + maxz); int sp = -1; for (size_t i = 0; i < S.W.P.size(); ++i) { double px = 0.5 * (S.W.P[i].lo.x + S.W.P[i].hi.x); if (std::fabs(px) < 0.3) { sp = (int)i; break; } }
          if (sp >= 0) { double px = 0.5 * (S.W.P[sp].lo.x + S.W.P[sp].hi.x), pz = 0.5 * (S.W.P[sp].lo.z + S.W.P[sp].hi.z); V2 d{px - mx, pz - mz}; double L = std::sqrt(d.x * d.x + d.z * d.z);
            std::vector<Ball> Bp{{0, mx, mz, 0, 0}}; Shot sp1; sp1.cueId = 0; sp1.dx = d.x / L; sp1.dz = d.z / L; sp1.F = 2.0; sp1.spin = -1; Result rp = S.run(Bp, &sp1, false, true);
            int np = 0; for (auto& e : rp.tev) if (e.kind == 3) { ++np; CHECK(e.pocket == sp, "%s: pocket index %d expected %d", t.key, e.pocket, sp); } CHECK(np == 1 && rp.balls[0].pocketed, "%s: pocket event count %d", t.key, np); } }
        // ---- detect: own pocket centres + own radius -> own key; wrong radius -> no match
        std::vector<V2> pk; for (auto& p : S.W.P) pk.push_back({0.5 * (p.lo.x + p.hi.x), 0.5 * (p.lo.z + p.hi.z)});
        std::string k = Sim::detect(dir, pk, t.R); CHECK(k == t.key, "%s: detect -> '%s'", t.key, k.c_str());
        std::string kw = Sim::detect(dir, pk, t.R + 0.002); CHECK(kw.empty(), "%s: wrong radius still matched '%s'", t.key, kw.c_str());
        std::vector<V2> few(pk.begin(), pk.begin() + 5); CHECK(Sim::detect(dir, few, t.R).empty(), "%s: 5 pockets must not match", t.key);
        std::vector<V2> shifted = pk; for (auto& q : shifted) q.x += 0.2; CHECK(Sim::detect(dir, shifted, t.R).empty(), "%s: shifted pockets must not match", t.key);
        printf("%-8s tev: wall=%d ball=%d pocket=%d rest=%d | detect ok\n", t.key, nW, nB, nP, nS);
    }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
