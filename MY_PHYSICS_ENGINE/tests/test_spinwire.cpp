#include "../src/pp2_spinwire.hpp"
using namespace pp2;
static int fails = 0, n = 0;
static void check(const char* nm, bool ok, const char* extra = "") { ++n; if (!ok) ++fails; printf("%s %s %s\n", ok ? "PASS" : "FAIL", nm, extra); }
int main(int argc, char** argv) {
    Sim S; if (!S.load(argc > 1 ? argv[1] : "data")) { puts("no data"); return 2; }
    std::vector<Ball> B = {{0, -0.5, 0.0, 0, 0}, {1, 0.3, 0.05, 0, 0}};
    Shot s; s.cueId = 0; s.dx = 1; s.dz = 0.1; s.F = 6;
    char buf[200];
    spinwire::Tip t0; t0.h = 0; t0.v = 0;
    Shot a = s; bool ok = spinwire::applyTip(S, B, a, t0);
    Prediction p0 = S.predict(B, s);
    snprintf(buf, sizeof buf, "factor=%.5f finalVel=%.5f angle=%.4f", a.spin, p0.finalVelocity, a.spinAngleDeg);
    check("centre tip evaluates", ok, buf);
    // edge/side tips stay finite and bounded by the pipeline maxima
    spinwire::Tip t1; t1.h = 30; t1.v = 0; Shot b = s; bool ok1 = spinwire::applyTip(S, B, b, t1);
    snprintf(buf, sizeof buf, "factor=%.5f angle=%.4f", b.spin, b.spinAngleDeg);
    check("side tip evaluates", ok1 && std::isfinite(b.spin) && std::isfinite(b.spinAngleDeg), buf);
    spinwire::Tip t2; t2.h = 0; t2.v = -30; Shot c = s; bool ok2 = spinwire::applyTip(S, B, c, t2);
    snprintf(buf, sizeof buf, "factor=%.5f angle=%.4f", c.spin, c.spinAngleDeg);
    check("bottom tip evaluates", ok2, buf);
    // the shot with the tip-derived spin runs through the engine
    Result R = S.run(B, &b, false);
    check("engine runs with tip spin", R.t > 0, "");
    // P2N: reset zone, ApplySpinAngelCombinationForEdge formula, currentDifference = |angle-90|, edge hits get intBallAngle + spinType 2
    { spinwire::Tip tr; tr.h = 1.0; tr.v = -1.0; Shot r = s; r.spin = 5; r.spinAngleDeg = 9; bool okr = spinwire::applyTip(S, B, r, tr);
      check("|h|,|v|<=1 -> ResetSpinValues (no spin)", okr && r.spin == -1 && r.spinAngleDeg == 0 && !r.spinApplied, ""); }
    { double e1 = spinwire::edgeAngle(-30, 45, 120), e2 = spinwire::edgeAngle(30, 45, 120), e3 = spinwire::edgeAngle(100, 45, 40);
      snprintf(buf, sizeof buf, "%.6f %.6f %.6f", e1, e2, e3);
      check("edgeAngle: ratio*0.5*(h<0?+a:-a), a=min(ang,180-ang), ratio clamped", std::fabs(e1 - 20.0) < 1e-9 && std::fabs(e2 + 20.0) < 1e-9 && std::fabs(e3 + 20.0) < 1e-9, buf); }
    { Prediction pb = S.predict(B, s); spinwire::Tip t; t.h = 20; t.v = 10; Shot x = s, y = s; spinwire::applyTip(S, B, x, t);
      spinwire::SceneCfg c; c.useGivenDifference = true; c.currentDifference = 0; spinwire::applyTip(S, B, y, t, c);
      snprintf(buf, sizeof buf, "angleInt=%d factor(|a-90|)=%.5f factor(0)=%.5f", pb.angleInt, x.spin, y.spin);
      check("currentDifference derived from intBallAngle changes the pipeline", pb.isBall && pb.angleInt != 90 && (x.spin != y.spin || x.spinAngleDeg != y.spinAngleDeg), buf); }
    { std::vector<Ball> E = {{0, -0.5, 0.0, 0, 0}}; Shot se; se.cueId = 0; se.dx = 0.3; se.dz = 1; se.F = 6; Prediction pe = S.predict(E, se);
      spinwire::Tip t; t.h = -30; t.v = 0; Shot x = se; bool oke = spinwire::applyTip(S, E, x, t);
      double want = spinwire::edgeAngle(-30, 60 * 0.75, pe.angleInt);
      snprintf(buf, sizeof buf, "edge=%d angleInt=%d angle=%.5f want=%.5f", !pe.isBall, pe.angleInt, x.spinAngleDeg, want);
      check("edge hit: intBallAngle computed, spin angle = edgeAngle", pe.valid && !pe.isBall && pe.angleInt > 0 && oke && std::fabs(x.spinAngleDeg - want) < 1e-9, buf); }
    printf("%d checks, %d failed\n", n, fails); return fails ? 1 : 0;
}
