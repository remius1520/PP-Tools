// Consistency tests for src/pp2_servertracker.hpp (serialized level5 tail + Simulate loop count).  Not a comparison with a real shot.
#include <cstdio>
#include <cmath>
#include "../src/pp2_engine.hpp"
static int fails = 0, total = 0;
static void chk(const char* n, bool ok, double a = 0, double b = 0) { ++total; if (!ok) { ++fails; std::printf("FAIL %s (%.9g vs %.9g)\n", n, a, b); } }
int main() {
    using namespace pp2;
    st::Tail t = st::parseTail(st::kLevel5Tail);
    chk("level5 substeps = 7", t.substeps == 7.0f, t.substeps, 7);
    chk("level5 stopSimulateThreshold = 0.002", t.stopSimulateThreshold == 0.002f, t.stopSimulateThreshold, 0.002);
    chk("level5 solvecount = 8", t.solvecount == 8, t.solvecount, 8);
    chk("level5 isSolving = false", !t.isSolving);
    chk("level5 min/max point +-0.0015", t.minXpoint == -0.0015f && t.maxXpoint == 0.0015f && t.minZpoint == -0.0015f && t.maxZpoint == 0.0015f);
    chk("level5 rack +-0.07", t.minXRack == -0.07f && t.maxXRack == 0.07f);
    // field offsets: first serialized float <-> managed 0x128; substeps = 0x128 + 6*4 = 0x140; threshold 0x144; solvecount 0x148; isSolving 0x14C
    chk("managed offsets match code (+0x140/+0x144/+0x148/+0x14C)", st::kManagedFirstField + 6 * 4 == 0x140 && st::kManagedFirstField + 7 * 4 == 0x144 && st::kManagedFirstField + 8 * 4 == 0x148 && st::kManagedFirstField + 9 * 4 == 0x14C);
    // loop count of 0x5DB8798: do{Simulate}while(substeps > i)
    chk("loop count 7.0 -> 7", st::substepCount(7.0f) == 7, st::substepCount(7.0f), 7);
    chk("loop count 1.0 -> 1", st::substepCount(1.0f) == 1);
    chk("loop count 0.5 -> 1 (do-while runs once)", st::substepCount(0.5f) == 1);
    chk("loop count 7.5 -> 8", st::substepCount(7.5f) == 8, st::substepCount(7.5f), 8);
    chk("loop count 0 / negative -> 0 (fcmp <= 0 returns)", st::substepCount(0.0f) == 0 && st::substepCount(-3.0f) == 0);
    // engine defaults are the level5 values; setSubsteps recomputes
    Cfg c;
    chk("Cfg default substeps = 7", c.substeps == 7 && c.substepsF == t.substeps);
    chk("Cfg default stop = level5 threshold", std::fabs(c.stop - t.stopSimulateThreshold) < 1e-9, c.stop, t.stopSimulateThreshold);
    chk("Cfg default solvePasses = level5 solvecount", c.solvePasses == t.solvecount);
    chk("dt = float32(fixedDelta)/7", c.dt == (double)(st::substepDt((float)c.fixedDelta, 7.0f)), c.dt, 0);
    chk("7 sub-steps cover fixedDelta (<1e-9 s)", std::fabs(c.dt * c.substeps - c.fixedDelta) < 1e-9, c.dt * c.substeps, c.fixedDelta);
    c.setSubsteps(1.0f); chk("setSubsteps(1) -> one step of fixedDelta", c.substeps == 1 && std::fabs(c.dt - c.fixedDelta) < 1e-9, c.dt, c.fixedDelta);
    std::printf("%d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
