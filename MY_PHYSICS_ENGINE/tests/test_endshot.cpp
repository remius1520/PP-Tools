// P2Z3: end-of-shot predicate = Ball.IsMoving (|linearVelocity| >= Ball.sleepThreshold), NOT ServerTracker.stopSimulateThreshold (docs/FINDINGS_P2Z3.md).
// Consistency tests of the engine against the decoded rule; NOT a comparison with a real shot.
#include <cstdio>
#include <cmath>
#include "../src/pp2_engine.hpp"
static int fails = 0, total = 0;
static void chk(const char* n, bool ok, double a = 0, double b = 0) { ++total; if (!ok) { ++fails; std::printf("FAIL %s (%.9g vs %.9g)\n", n, a, b); } }
int main(int argc, char** argv) {
    using namespace pp2; Sim S; if (!S.load(argc > 1 ? argv[1] : "data")) { std::printf("cannot load data\n"); return 2; }
    chk("default ballStop = Ball.sleepThreshold from the runtime dumps (0.002f)", std::fabs(S.C.ballStop - 0.002) < 1e-12, S.C.ballStop, 0.002);
    chk("default stop (snap) = ServerTracker.stopSimulateThreshold (0.002f)", std::fabs(S.C.stop - 0.002) < 1e-12, S.C.stop, 0.002);
    // 1) free ball: how far it travels depends on ballStop, NOT on the snap threshold `stop`
    auto dist = [&](double stop, double ballStop) { Sim T = S; T.C.stop = stop; T.C.ballStop = ballStop; T.C.solveOverlap = false; Result q = T.run({{0, -0.5, -0.2, 0.3, 0.0}}, nullptr, false); return q.balls[0].x - (-0.5); };
    double dBase = dist(0.002, 0.002), dStopBig = dist(0.5, 0.002), dBallBig = dist(0.002, 0.05);
    chk("stop (snap threshold) does not change where a free ball ends", std::fabs(dBase - dStopBig) < 1e-12, dBase, dStopBig);
    chk("ballStop does: ending at 0.05 m/s leaves ~(0.05-0.002)/drag less travel", std::fabs((dBase - dBallBig) - (0.05 - 0.002) / S.C.drag) < 0.01, dBase - dBallBig, (0.05 - 0.002) / S.C.drag);
    // 2) boundary: IsMoving is >= (cset ge at 0x5DD66AC): a ball exactly at the threshold still counts as moving
    { Sim T = S; T.C.ballStop = 0.5; T.C.solveOverlap = false; Result r = T.run({{0, 0, 0, 0.5, 0}}, nullptr, false);
      chk("|v| == ballStop counts as moving (>=)", r.balls[0].x > 1e-6, r.balls[0].x, 0); }
    { Sim T = S; T.C.ballStop = 0.5; T.C.solveOverlap = false; Result r = T.run({{0, 0, 0, 0.4999, 0}}, nullptr, false);
      chk("|v| just under ballStop: already stopped", std::fabs(r.balls[0].x) < 1e-12, r.balls[0].x, 0); }
    // 3) special path: with the default (ballStop == stop) the open shot snaps onto baseline+F; with a larger ballStop the shot is declared over (BallInAction false -> SleepBalls) just before the snap
    { Shot sh; sh.cueId = 0; sh.dx = 1; sh.dz = 0; sh.F = 0.5; Result r = S.run({{0, 0, 0, 0, 0}}, &sh, false);
      chk("open shot, default thresholds: snaps onto baseline+F", std::fabs(r.balls[0].x - 0.5) < 1e-6, r.balls[0].x, 0.5);
      Sim T = S; T.C.ballStop = 0.05; Result q = T.run({{0, 0, 0, 0, 0}}, &sh, false);
      chk("open shot, ballStop 0.05: ends before the snap (|target-pos| < 0.05)", q.balls[0].x > 0.44 && q.balls[0].x < 0.4999, q.balls[0].x, 0.45); }
    std::printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
