// Consistency tests for src/pp2_clientpredict.hpp (formulas decoded from ARM64). Not a comparison with a real shot.
#include <cstdio>
#include <cmath>
#include "../src/pp2_clientpredict.hpp"
using namespace pp2::cp;
static int fails = 0, total = 0;
static void chk(const char* n, bool ok, double a = 0, double b = 0) { ++total; if (!ok) { ++fails; std::printf("FAIL %s (%.6f vs %.6f)\n", n, a, b); } }
static bool near(double a, double b, double e = 1e-5) { return std::fabs(a - b) <= e; }
int main() {
    // target direction = -normalize_xz(n); origin = hit - n*r (y = 0)
    { Ray r = predictTargetBallAfterCollision({0.3f, 0.0f, 0.1f}, 0.0275f, {-0.6f, 0.4f, -0.8f});
      chk("tgt dir x", near(r.dir.x, 0.6), r.dir.x, 0.6); chk("tgt dir z", near(r.dir.z, 0.8), r.dir.z, 0.8); chk("tgt dir y", r.dir.y == 0);
      chk("tgt origin x", near(r.origin.x, 0.3 + 0.6 * 0.0275), r.origin.x); chk("tgt origin z", near(r.origin.z, 0.1 + 0.8 * 0.0275), r.origin.z); chk("tgt origin y", r.origin.y == 0); }
    { Ray r = predictTargetBallAfterCollision({1, 2, 3}, 0.0275f, {0, 1, 0}); chk("tgt degenerate dir 0", r.dir.x == 0 && r.dir.z == 0); }
    // head-on: u = +x, n = -x  => ballAngle 90 (target gets full sin), half-ball 30deg offset => acos stays in [0,180]
    { float a = ballAngleDeg({1, 0, 0}, {-1, 0, 0}); chk("angle head-on = 90", near(a, 90.0, 1e-3), a, 90);
      float s = (float)std::sin(30 * 0.0174532925), c = (float)std::cos(30 * 0.0174532925);
      float b = ballAngleDeg({1, 0, 0}, {-c, 0, s}); chk("angle offset in range", b > 0 && b < 180, b, 0);
      chk("angle symmetric sign", near(ballAngleDeg({1,0,0},{-c,0,-s}), 180.0 - b, 1e-2), ballAngleDeg({1,0,0},{-c,0,-s}), 180 - b); }
    // classic path (no spin): rotY(n, 180) of head-on normal -> cue goes back (+x)
    { V3 v = predictCueDirAfterBallCollision(eulerY(180 * kDeg2Rad), 0.f, {-1, 0, 0}); chk("ball classic 180 head-on", near(v.x, 1, 1e-5) && near(v.z, 0, 1e-5), v.x, v.z); }
    // spin replaces classic when SpinAngelDeg != 0
    { V3 a = predictCueDirAfterBallCollision(eulerY(265 * kDeg2Rad), 0.f, {-1, 0, 0}), b = predictCueDirAfterBallCollision(eulerY(265 * kDeg2Rad), 30.f, {-1, 0, 0});
      V3 e = rotate(eulerY(30 * kDeg2Rad), {-1, 0, 0});
      chk("ball spin replaces classic", near(b.x, e.x) && near(b.z, e.z) && !(near(a.x, b.x) && near(a.z, b.z))); }
    // edge: reflection of the shot direction, y dropped; spin 0 = identity
    { V3 r = predictCueDirAfterEdgeCollision(0.f, {0.6f, 0.f, 0.8f}, {-1, 0, 0}); chk("edge reflect", near(r.x, -0.6) && near(r.z, 0.8) && r.y == 0, r.x, r.z);
      V3 r2 = predictCueDirAfterEdgeCollision(90.f, {0.6f, 0.f, 0.8f}, {-1, 0, 0}); chk("edge + spin 90 rotates", near(std::hypot(r2.x, r2.z), 1.0, 1e-4) && !near(r2.x, r.x, 1e-3)); }
    // edge uses n.y in the dot product (as the binary does)
    { V3 r = predictCueDirAfterEdgeCollision(0.f, {1, 1, 0}, {0, 1, 0}); chk("edge dot uses y", near(r.x, 1) && near(r.z, 0) && r.y == 0, r.x, r.z); }
    // normalise + fallback
    { V3 f = forceDirectionAfterCollided({0, 3, 4}, {9, 9, 9}); chk("normalize xyz", near(f.y, 0.6) && near(f.z, 0.8)); V3 g = forceDirectionAfterCollided({0, 0, 1e-7f}, {9, 8, 7}); chk("fallback kept", g.x == 9 && g.y == 8 && g.z == 7); }
    // rotate() is a pure rotation about Y with Unity handedness: Euler(0,90,0)*(0,0,1) = (1,0,0)
    { V3 v = rotate(eulerY(90 * kDeg2Rad), {0, 0, 1}); chk("Unity handedness", near(v.x, 1, 1e-5) && near(v.z, 0, 1e-5), v.x, v.z); }
    { // P2R: Euler(spin).y != 0  <=>  SpinAngelDeg != 0, over every value GetSpinAngelMapping can produce on a 0.1-degree grid (0.1 .. 360.0) and at 0
      int bad = 0; for (int i = 1; i <= 3600; ++i) { float a = i * 0.1f; if (!(eulerYDegPositive(eulerY(a * kDeg2Rad)) != 0.f)) ++bad; }
      chk("Euler(spin).y != 0 for every SpinAngelDeg in 0.1..360 (grid 0.1)", bad == 0, bad, 0);
      chk("Euler(spin).y == 0 at SpinAngelDeg 0", eulerYDegPositive(eulerY(0.f)) == 0.f);
      chk("Euler(spin).y at 180 deg is ~180, not 0", near(eulerYDegPositive(eulerY(180.f * kDeg2Rad)), 180.0, 1e-2), eulerYDegPositive(eulerY(180.f * kDeg2Rad)), 180);
      chk("Euler(spin).y at 270 deg wraps to ~270", near(eulerYDegPositive(eulerY(270.f * kDeg2Rad)), 270.0, 1e-2), eulerYDegPositive(eulerY(270.f * kDeg2Rad)), 270); }
    std::printf("%d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
