// Consistency tests for the ServerTracker.OnServerSimulate special path in src/pp2_engine.hpp (P2R, docs/FINDINGS_P2R.md).  NOT a comparison with a real shot.
#include <cstdio>
#include <cmath>
#include "../src/pp2_engine.hpp"
static int fails = 0, total = 0;
static void chk(const char* n, bool ok, double a = 0, double b = 0) { ++total; if (!ok) { ++fails; std::printf("FAIL %s (%.9g vs %.9g)\n", n, a, b); } }
int main(int argc, char** argv) {
    using namespace pp2; Sim S; if (!S.load(argc > 1 ? argv[1] : "data")) { std::printf("cannot load data\n"); return 2; }
    const double fd = (double)(float)S.C.fixedDelta;
    // 1) open table, short shot: nothing to hit within |vec| -> flag false -> v = target-pos each tick, snap when < 0.002
    { Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0; sh.F = 0.5; Result r = S.run({{0, 0, 0, 0, 0}}, &sh, true);
      chk("open shot: flag108 false", !r.flag108);
      chk("open shot: ends exactly on target baseline+vec", std::fabs(r.balls[0].x - 0.5) < 1e-6 && std::fabs(r.balls[0].z) < 1e-9, r.balls[0].x, 0.5);
      double d = 0.5; long n = 0; while (!(d < S.C.stop)) { d -= d * (1 - S.C.drag * fd) * fd; ++n; }   // recurrence of the special path (damping first, then x += v dt)
      chk("open shot: tick count = recurrence (independent re-derivation)", std::fabs(r.t - n * fd) < 1e-6, r.t, n * fd);
      Shot sh0 = sh; Sim S0 = S; S0.C.special = false; Result r0 = S0.run({{0, 0, 0, 0, 0}}, &sh0, false);
      chk("old behaviour (special=false) overshoots F: travels ~F/0.62", r0.balls[0].x > 0.78 && r0.balls[0].x < 0.82, r0.balls[0].x, 0.806); }
    // 2) shot into the far cushion: flag true, 7 sub-steps per tick, speed at distance s = F - s
    { Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0; sh.F = 3.0; Result r = S.run({{0, 0, 0, 0, 0}}, &sh, true);
      chk("cushion shot: flag108 true", r.flag108);
      double maxRel = 0; int cnt = 0; double px = 0;
      for (auto& p : r.path) { double x = p.p[0].x; if (x >= r.pred.hitDistance - 0.1) break; if (p.t > 0.05) { double v = (x - px) / fd, s = 0.5 * (x + px), want = 3.0 - s; maxRel = std::max(maxRel, std::fabs(v - want) / want); ++cnt; } px = x; }
      chk("cushion shot: path speed = F - s before contact (<0.5% incl. in-tick damping)", cnt > 20 && maxRel < 0.005, maxRel, 0.005);
      chk("cushion shot: predicted contact speed = F - hitDist", std::fabs(r.pred.cueV.x * -1 - (3.0 - r.pred.hitDistance)) < 1e-9 || std::fabs(std::fabs(r.pred.cueV.x) - (3.0 - r.pred.hitDistance)) < 1e-9, std::fabs(r.pred.cueV.x), 3.0 - r.pred.hitDistance); }
    // 3) castHits: ball on the line within F -> true; beyond F -> false; off the line -> false
    { std::vector<Ball> B = {{0, 0, 0, 0, 0}, {1, 0.5, 0, 0, 0}};
      chk("castHits: ball ahead within |vec|", S.castHits(B, 0, {0, 0}, {1, 0}, 1.0));
      chk("castHits: ball beyond |vec|", !S.castHits(B, 0, {0, 0}, {1, 0}, 0.3));
      B[1].z = 0.5; chk("castHits: ball off the line (perp > 2R+2*offset)", !S.castHits(B, 0, {0, 0}, {1, 0}, 1.0));
      B[1].z = 2 * S.C.R + 2 * S.C.contactOffset - 1e-4; chk("castHits: inflated radius catches a near miss", S.castHits(B, 0, {0, 0}, {1, 0}, 1.0));
      B[1].z = 2 * S.C.R + 2 * S.C.contactOffset + 1e-4; chk("castHits: just outside the inflated radius", !S.castHits(B, 0, {0, 0}, {1, 0}, 1.0)); }
    // 4) after the cue's first contact: one Simulate(fixedDelta) per tick (path sampled every tick, t = k*fd)
    { Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0; sh.F = 3.0; Result r = S.run({{0, 0, 0, 0, 0}}, &sh, true);
      bool ok = !r.path.empty(); for (size_t i = 0; i < r.path.size(); ++i) if (std::fabs(r.path[i].t - (i + 1) * fd) > 1e-7) { ok = false; break; }
      chk("path timestamps are k*fixedDelta for the whole shot (special + normal)", ok); }
    // 5) layer mask 0xC00 (balls 10 | edges 11): layer-0 table meshes never set the flag; masking out layer 11 removes the cushion
    { Sim S2 = S; S2.C.castMask = 0x400; std::vector<Ball> B = {{0, 0, 0, 0, 0}};
      chk("castHits: edges (layer 11) in mask -> cushion 1.08 m ahead found with |vec| = 3", S.castHits(B, 0, {0, 0}, {1, 0}, 3.0));
      chk("castHits: mask without layer 11 -> cushion not found", !S2.castHits(B, 0, {0, 0}, {1, 0}, 3.0));
      S2.C.castMask = 0x800; B.push_back({1, 0.5, 0, 0, 0}); chk("castHits: mask without layer 10 ignores balls (|vec| short of the cushion)", !S2.castHits(B, 0, {0, 0}, {1, 0}, 0.9)); }
    std::printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
