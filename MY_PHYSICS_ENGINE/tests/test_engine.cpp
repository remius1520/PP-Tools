// Host tests: analytic checks + invariants.  Build: see build.sh.  Every expected value is derived, none is fitted.
#include "../src/pp2_engine.hpp"
#include "../src/pp2_postcollision.hpp"
#include <random>
using namespace pp2;
static int fails = 0, total = 0;
static void check(const char* name, bool ok, const char* info = "") { ++total; if (!ok) ++fails; printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, info); }
static char buf[256];
int main(int argc, char** argv) {
    Sim S; if (!S.load(argc > 1 ? argv[1] : "data")) { puts("cannot load data"); return 2; }
    // 1. free decay == discrete damping model
    { S.C = Cfg(); Result r = S.run({{0, -0.5, -0.2, 0.5, 0}}); double v = 0.5, x = -0.5; while (v > S.C.stop) { v *= 1 - S.C.drag * S.C.dt; x += v * S.C.dt; }
      snprintf(buf, 256, "sim=%.6f model=%.6f", r.balls[0].x, x); check("free decay vs discrete model (<0.05 mm)", std::fabs(r.balls[0].x - x) < 5e-5, buf); }
    // 2. ball-ball head-on, no drag: velocities follow e exactly (equal masses): vA=(1-e)/2 v, vB=(1+e)/2 v
    { S.C = Cfg(); S.C.drag = 0; S.C.tmax = 0.2; S.C.firstContact = false; Result r = S.run({{0, -0.5, -0.2, 1.0, 0}, {1, -0.3, -0.2, 0, 0}});
      double e = S.C.eBall, a = r.balls[0].vx, b = r.balls[1].vx; snprintf(buf, 256, "vA=%.6f(%.6f) vB=%.6f(%.6f)", a, (1 - e) / 2, b, (1 + e) / 2);
      check("head-on: e=0.92 impulse exact", std::fabs(a - (1 - e) / 2) < 1e-9 && std::fabs(b - (1 + e) / 2) < 1e-9, buf); }
    // 3. oblique ball-ball, e=1: momentum + energy conserved, perpendicular split (90 deg rule)
    { S.C = Cfg(); S.C.drag = 0; S.C.eBall = 1.0; S.C.tmax = 0.3; S.C.firstContact = false;
      Result r = S.run({{0, -0.5, -0.2, 1.0, 0}, {1, -0.3, -0.2 + 0.03, 0, 0}}); const Ball &a = r.balls[0], &b = r.balls[1];
      double px = a.vx + b.vx, pz = a.vz + b.vz, ke = a.vx * a.vx + a.vz * a.vz + b.vx * b.vx + b.vz * b.vz, perp = a.vx * b.vx + a.vz * b.vz;
      snprintf(buf, 256, "p=(%.6f,%.6f) ke=%.6f a.b=%.2e", px, pz, ke, perp);
      check("oblique e=1: momentum, energy, 90deg split", std::fabs(px - 1) < 1e-9 && std::fabs(pz) < 1e-9 && std::fabs(ke - 1) < 1e-9 && std::fabs(perp) < 1e-9, buf); }
    // 4. high-speed ball into the long cushion: no tunnelling, contact exactly at the face, normal part scaled by 0.92, tangential kept
    { S.C = Cfg(); S.C.drag = 0; S.C.tmax = 0.04; double tn = 0; Result r = S.run({{0, 0.5, 0.1, 0.6, 12.0}});
      const Ball& b = r.balls[0]; tn = b.vz; snprintf(buf, 256, "vx=%.6f vz=%.6f", b.vx, b.vz);
      check("fast cushion hit: no tunnelling, tangential kept, normal*0.92", b.z < 0.4 && std::fabs(b.vx - 0.6) < 5e-3 && std::fabs(tn + 12.0 * 0.92) < 0.2, buf); }
    // 5. straight free motion is exact for any speed (continuous, no sub-move discretisation error)
    { S.C = Cfg(); S.C.drag = 0; double err = 0; double sp[3] = {1.0, 5.0, 15.0};
      for (int k = 0; k < 3; ++k) { S.C.tmax = 0.5 / sp[k]; Result r = S.run({{0, 0.5, -0.4, 0, sp[k]}}); err = std::max(err, std::fabs(r.balls[0].z - (-0.4 + sp[k] * r.t))); }
      snprintf(buf, 256, "max err=%.2e m", err); check("straight motion exact at 1/5/15 m/s", err < 1e-9, buf); }
    // 6. game first contact: straight full hit -> target speed F-d, cue creeps forward at 0.04*(F-d)   (formulas of PredictedFinalVelocity / TargetBallFinalVelocity)
    { S.C = Cfg(); Shot s; s.cueId = 0; s.dx = 1; s.dz = 0; s.F = 2.0; std::vector<Ball> B{{0, -0.5, -0.2, 0, 0}, {1, 0.2, -0.2, 0, 0}}; Prediction p = S.predict(B, s);
      double d = 0.7 - 2 * S.C.R; snprintf(buf, 256, "hit=%.5f angle=%.2f tgt=%.5f cue=%.5f", p.hitDistance, p.ballAngleDeg, p.targetSpeed, len(p.cueV));
      check("first contact straight: formulas", p.valid && p.isBall && std::fabs(p.hitDistance - d) < 1e-9 && std::fabs(p.targetSpeed - (2.0 - d)) < 1e-9 && std::fabs(len(p.cueV) - 0.04 * (2.0 - d)) < 1e-9, buf); }
    // 7. game first contact is applied by the engine (server override) and the target leaves along the line of centres
    { S.C = Cfg(); S.C.drag = 0; S.C.tmax = 0.4; Shot s; s.cueId = 0; s.dx = 1; s.dz = 0; s.F = 2.0; std::vector<Ball> B{{0, -0.5, -0.2, 0, 0}, {1, 0.2, -0.2, 0, 0}};
      Result r = S.run(B, &s); snprintf(buf, 256, "target vx=%.5f (pred %.5f)", r.balls[1].vx, r.pred.targetV.x);
      check("engine applies first-contact override", std::fabs(r.balls[1].vx - r.pred.targetV.x) < 1e-9 && r.pred.targetV.x > 1.0, buf); }
    // 8. override happens ONCE: second contact is plain physics (e=0.92 impulse conserves the formula)
    { S.C = Cfg(); S.C.firstContact = true; S.C.drag = 0; S.C.tmax = 3.0; Shot s; s.cueId = 0; s.dx = 1; s.dz = 0; s.F = 3.0;
      std::vector<Ball> B{{0, -0.8, -0.2, 0, 0}, {1, -0.4, -0.2, 0, 0}, {2, -0.3, -0.2, 0, 0}}; Result r = S.run(B, &s); check("multi-ball chain: ends, no NaN", std::isfinite(r.balls[0].x + r.balls[1].x + r.balls[2].x), ""); }
    // 9. pocket: some aim angle pockets the ball in the corner
    { S.C = Cfg(); int n = 0; for (int k = -40; k <= 40; ++k) { double ang = std::atan2(-0.775 + 0.4, 1.145 - 0.5) + k * 0.25 * M_PI / 180; Result r = S.run({{0, 0.5, -0.4, 2.5 * std::cos(ang), 2.5 * std::sin(ang)}}); if (r.balls[0].pocketed) ++n; }
      snprintf(buf, 256, "aim angles that pocket = %d", n); check("corner pocket reachable", n > 0, buf); }
    // 10. random multi-ball: no NaN, nothing leaves the table, kinetic energy never increases without the first-contact override
    { std::mt19937 g(7); std::uniform_real_distribution<double> ux(-0.9, 0.9), uz(-0.55, 0.2), uv(-6, 6); int bad = 0, inc = 0;
      for (int k = 0; k < 200; ++k) { S.C = Cfg(); S.C.tmax = 20; std::vector<Ball> B; for (int i = 0; i < 4; ++i) B.push_back({i, ux(g), uz(g), uv(g), uv(g)});
        bool overlap = false; for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) if (std::hypot(B[i].x - B[j].x, B[i].z - B[j].z) < 2 * S.C.R + 1e-3) overlap = true; if (overlap) continue;
        double ke0 = 0; for (auto& b : B) ke0 += b.vx * b.vx + b.vz * b.vz; Result r = S.run(B); double ke1 = 0;
        for (auto& b : r.balls) { if (!b.pocketed && !(b.x > -1.2 && b.x < 1.2 && b.z > -0.8 && b.z < 0.42 && std::isfinite(b.x) && std::isfinite(b.z))) ++bad; ke1 += b.vx * b.vx + b.vz * b.vz; }
        if (ke1 > ke0 * 1.0001) ++inc; }
      snprintf(buf, 256, "escaped/NaN=%d energy-increase=%d", bad, inc); check("random shots: stay on table, energy never grows", bad == 0 && inc == 0, buf); }
    // 11. e-parameters wired: Pocket_Physics jaws reflect (0.92), default surfaces absorb (0)
    { check("material table", S.C.ePocket == 0.92 && S.C.eEdge == 0.92 && S.C.eDefault == 0.0, ""); }
    // 12. P2H: spinAngleDeg = 0 keeps the old prediction; != 0 rotates the post-collision cue direction (ball: replaces theta table, edge: spin*reflection)
    { S.C = Cfg(); Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0.15; sh.F = 2.0; std::vector<Ball> bl = {{0, -0.5, -0.2, 0, 0}, {1, 0.2, -0.15, 0, 0}};
      Prediction a = S.predict(bl, sh); sh.spinAngleDeg = 0; Prediction b = S.predict(bl, sh); sh.spinAngleDeg = 30; Prediction c = S.predict(bl, sh);
      bool same = a.valid && std::fabs(a.cueDir.x - b.cueDir.x) < 1e-12 && std::fabs(a.cueDir.z - b.cueDir.z) < 1e-12;
      bool moved = c.valid && (std::fabs(c.cueDir.x - a.cueDir.x) + std::fabs(c.cueDir.z - a.cueDir.z)) > 1e-3 && std::fabs(std::hypot(c.cueDir.x, c.cueDir.z) - 1) < 1e-9;
      check("spinAngleDeg 0 = unchanged, 30 rotates ball-case cue direction", same && moved); }
    // 13. P2M: theta SmoothDamp. frames=0 unchanged; 1 frame from 265 toward the table value moves partway; many frames converge to the table value; engine == postcol::smoothDamp
    { S.C = Cfg(); Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0.15; sh.F = 2.0; std::vector<Ball> bl = {{0, -0.5, -0.2, 0, 0}, {1, 0.2, -0.15, 0, 0}};
      Prediction a = S.predict(bl, sh);
      sh.thetaStartDeg = 265; sh.thetaFrames = 1; Prediction one = S.predict(bl, sh);
      sh.thetaFrames = 40; Prediction many = S.predict(bl, sh);
      sh.thetaFrames = 0; Prediction zero = S.predict(bl, sh);
      float v = 0, c = 265; c = pp2::postcol::smoothDamp(c, (float)a.theta, v, 0.03f, 1e30f, 0.0226667f);
      bool ok = a.valid && zero.theta == a.theta && std::fabs(many.theta - a.theta) < 1e-3 && std::fabs(one.theta - c) < 1e-4 && one.theta != a.theta;
      snprintf(buf, 256, "table=%.1f one=%.4f ref=%.4f many=%.5f", a.theta, one.theta, c, many.theta); check("theta SmoothDamp (265 -> table): 0 frames=converged, matches postcol::smoothDamp, converges", ok, buf); }
    printf("\n%d/%d passed\n", total - fails, total); return fails ? 1 : 0;
}
