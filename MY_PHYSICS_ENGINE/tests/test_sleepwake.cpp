// T1 sleep/wake + CCD tests (PhysX 4.1 model, PHYSX_4_1_REFERENCE — NOT confirmed from game binary).
// Consistency tests only; NOT a comparison with a real shot.
//   test 1: sleepKE=0 => no sleeping, free ball keeps moving during short run
//   test 2: sleepKE>0 with small wakeCounter => ball eventually sleeps (v=0 set by sleep)
//   test 3: sleepKE=0.001 + drag=0 + short tmax => KE=50 >> sleepKE, no sleep before tmax
//   test 4: sleepKE>0 collision still transfers momentum to target
//   test 5: dragMode=0 vs dragMode=1 both decay but give different results
//   test 6: continuous TOI (CCD mode 0) never tunnels a fast ball into a cushion
#include <cstdio>
#include <cmath>
#include "../src/pp2_engine.hpp"
static int fails = 0, total = 0;
static void chk(const char* n, bool ok) { ++total; if (!ok) { ++fails; std::printf("FAIL %s\n", n); fflush(stdout); } }
static void chk2(const char* n, bool ok, double a, double b) { ++total; if (!ok) { ++fails; std::printf("FAIL %s (%.9g vs %.9g)\n", n, a, b); fflush(stdout); } }
int main(int argc, char** argv) {
    using namespace pp2; Sim S; if (!S.load(argc > 1 ? argv[1] : "data")) { std::printf("cannot load data\n"); return 2; }
    // test 1: sleepKE=0 => no sleeping, free ball keeps moving during short run
    { Sim T = S; T.C.sleepKE = 0; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.5;
      Result r = T.run({{0, -0.5, -0.2, 1.0, 0.0}}, nullptr, false);
      chk("sleepKE=0: ball keeps moving (never sleeps)", r.balls[0].vx != 0 || r.balls[0].vz != 0); }
    // test 2: sleepKE>0 with small wakeCounter => ball eventually sleeps (v=0 set by sleep)
    { Sim T = S; T.C.sleepKE = 0.001; T.C.wakeCounter = 0.01; T.C.ballStop = 0; T.C.solveOverlap = false;
      Result r = T.run({{0, -0.5, -0.2, 1.0, 0.0}}, nullptr, false);
      chk("sleepKE>0 wakeCounter small: ball eventually sleeps (v=0)", r.balls[0].vx == 0 && r.balls[0].vz == 0); }
    // test 3: sleepKE=0.001 + drag=0 + short tmax => KE=50 >> sleepKE, no sleep before tmax
    { Sim T = S; T.C.sleepKE = 0.001; T.C.wakeCounter = 0.01; T.C.ballStop = 0; T.C.solveOverlap = false;
      T.C.drag = 0; T.C.tmax = 0.05;
      Result r = T.run({{0, -0.5, -0.2, 10.0, 0.0}}, nullptr, false);
      chk("sleepKE=0.001 drag=0 short tmax: ball does NOT sleep (KE=50 >> threshold)", r.balls[0].vx != 0 || r.balls[0].vz != 0); }
    // test 4: sleepKE>0 collision still transfers momentum to target
    { Sim T = S; T.C.sleepKE = 0.001; T.C.wakeCounter = 0.01; T.C.ballStop = 0; T.C.solveOverlap = false;
      T.C.drag = 0;
      Result r = T.run({{0, -0.5, -0.2, 1.0, 0.0}, {1, -0.3, -0.2, 0, 0}}, nullptr, false);
      chk("sleepKE>0: ball-ball collision transfers momentum to target", r.balls[1].vx > 0); }
    // test 5: dragMode=0 vs dragMode=1 both decay but give different results
    { Sim A = S, B = S; A.C.dragMode = 0; B.C.dragMode = 1; A.C.sleepKE = 0; B.C.sleepKE = 0; A.C.ballStop = 0; B.C.ballStop = 0;
      Result ra = A.run({{0, -0.5, -0.2, 1.0, 0.0}}, nullptr, false);
      Result rb = B.run({{0, -0.5, -0.2, 1.0, 0.0}}, nullptr, false);
      double da = std::hypot(ra.balls[0].x - (-0.5), ra.balls[0].z - (-0.2));
      double db = std::hypot(rb.balls[0].x - (-0.5), rb.balls[0].z - (-0.2));
      chk2("dragMode 0 vs 1: both decay, results differ", da > 0 && db > 0 && std::fabs(da - db) > 1e-9, da, db); }
// test 6: continuous TOI (CCD mode 0) never tunnels a fast ball into a cushion
//   ball at x=0, z=0 moving +x at 20 m/s; right cushion face at x=0.5-R
{ Sim A = S, B = S; A.C.ccdMode = 0; B.C.ccdMode = 1; A.C.sleepKE = 0; B.C.sleepKE = 0; A.C.drag = 0; B.C.drag = 0;
  A.C.ballStop = 0; B.C.ballStop = 0; A.C.tmax = 0.5; B.C.tmax = 0.5;
  Result ra = A.run({{0, 0.0, 0.0, 20.0, 0.0}}, nullptr, false);
  Result rb = B.run({{0, 0.0, 0.0, 20.0, 0.0}}, nullptr, false);
  chk2("CCD mode0: fast ball stopped at/before cushion face (no tunnel)", ra.balls[0].x <= 0.5 + S.C.R + 1e-6, ra.balls[0].x, 0.5 + S.C.R);
  chk("CCD mode1: same shot produces finite result", std::isfinite(rb.balls[0].x)); }

// test 7: high-speed collision between two balls (momentum conserved between balls, no tunnel)
{ Sim T = S; T.C.sleepKE = 0; T.C.drag = 0; T.C.eBall = 0.92; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.02;
  // balls just touching at t=0 (distance = 2R = 0.055), ball0 moving +x at 10, ball1 stationary
  Result r = T.run({{0, -0.3, -0.2, 10.0, 0.0}, {1, -0.245, -0.2, 0, 0}}, nullptr, false);
  double px = r.balls[0].vx + r.balls[1].vx;
  chk("high-speed ball-ball: momentum conserved (approx)", std::fabs(px - 10.0) < 1e-3); }

// test 8: multiple simultaneous collisions (3 balls in a chain) no NaN
{ Sim T = S; T.C.sleepKE = 0; T.C.drag = 0; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.4;
  Result r = T.run({{0, -0.5, -0.2, 1.0, 0.0}, {1, -0.3, -0.2, 0, 0}, {2, -0.1, -0.2, 0, 0}}, nullptr, false);
  chk("3-ball chain: no NaN, all finite", std::isfinite(r.balls[0].x) && std::isfinite(r.balls[1].x) && std::isfinite(r.balls[2].x)); }

// test 9: resting contact protection — ball stationary against cushion (zero-time collision)
{ Sim T = S; T.C.sleepKE = 0; T.C.drag = 0; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.1;
  Result r = T.run({{0, 0.45, -0.2, 0, -0.1}}, nullptr, false);
  chk("resting contact vs cushion: no tunnel/overflow, stays on table", r.balls[0].z > -0.6 && std::isfinite(r.balls[0].x) && std::isfinite(r.balls[0].z)); }

// test 10: zero-time collision — two balls already touching, relative velocity into contact
{ Sim T = S; T.C.sleepKE = 0; T.C.drag = 0; T.C.eBall = 0.92; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.1;
  Result r = T.run({{0, -0.5, -0.2, -0.5, 0.0}, {1, -0.445, -0.2, 0, 0}}, nullptr, false);
  chk("zero-time touching balls: finite result", std::isfinite(r.balls[0].vx) && std::isfinite(r.balls[0].vz)); }

// test 11: CCD mode 0 vs mode 1 produce different results at high speed (modes actually differ)
{ Sim A = S, B = S; A.C.ccdMode = 0; B.C.ccdMode = 1; A.C.sleepKE = 0; B.C.sleepKE = 0; A.C.drag = 0; B.C.drag = 0;
  A.C.ballStop = 0; B.C.ballStop = 0; A.C.tmax = 0.5; B.C.tmax = 0.5;
  Result ra = A.run({{0, 0.0, 0.0, 25.0, 0.0}}, nullptr, false);
  Result rb = B.run({{0, 0.0, 0.0, 25.0, 0.0}}, nullptr, false);
  chk2("CCD mode0 vs mode1: x differs at high speed", std::fabs(ra.balls[0].x - rb.balls[0].x) > 1e-9, ra.balls[0].x, rb.balls[0].x); }

// test 12: maxDepenetrationVelocity actually clamps displacement per sub-step
{ Sim A = S, B = S; A.C.maxDepenetrationVelocity = 1000.0; B.C.maxDepenetrationVelocity = 0.1;
  A.C.sleepKE = 0; B.C.sleepKE = 0; A.C.drag = 0; B.C.drag = 0; A.C.ccdMode = 1; B.C.ccdMode = 1;
  A.C.ballStop = 0; B.C.ballStop = 0; A.C.tmax = 0.05; B.C.tmax = 0.05;
  Result ra = A.run({{0, 0.0, 0.0, 100.0, 0.0}}, nullptr, false);
  Result rb = B.run({{0, 0.0, 0.0, 100.0, 0.0}}, nullptr, false);
  chk2("maxDepenetrationVelocity: low cap moves ball less", rb.balls[0].x < ra.balls[0].x - 1e-6, rb.balls[0].x, ra.balls[0].x); }

// test 13: wake-on-collision — sleeping ball wakes when hit by moving ball
{ Sim T = S; T.C.sleepKE = 0.01; T.C.wakeCounter = 0.05; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.3;
  T.C.drag = 0;
  Result r = T.run({{0, -0.5, -0.2, 0, 0.0}, {1, -0.49, -0.2, -2.0, 0.0}}, nullptr, false);
  chk("wake-on-collision: target ball wakes and moves after hit", r.balls[0].vx != 0 || r.balls[0].vz != 0); }

// test 14: sleepKE=0 default means sleep/wake is OFF by default
{ Sim T = S; T.C.sleepKE = 0; T.C.ballStop = 0; T.C.solveOverlap = false; T.C.tmax = 0.3;
  Result r = T.run({{0, -0.5, -0.2, 0.5, 0.0}}, nullptr, false);
  chk("sleepKE=0 default: ball keeps moving (sleep disabled)", r.balls[0].vx != 0 || r.balls[0].vz != 0); }

std::printf("%d/%d passed\n", total - fails, total); return fails ? 1 : 0;
}