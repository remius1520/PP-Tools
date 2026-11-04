// v92: Guard calls Host::prepare() once after the snapshot and Host::finish() once AFTER every ball was put back (kinematic bodies become dynamic only for the prediction).
#include "../native/live.hpp"
#include <cstdio>
#include <string>
#include <vector>
using namespace pp2::live;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s  (%s)\n", msg, #c); } } while (0)
struct H : Host {
    std::vector<std::string> log; BallState b;
    H() { b.id = 0; b.pos = {0.1, 0, 0.2}; b.active = true; }
    int count() override { return 1; }
    bool get(int, BallState& s) override { s = b; return true; }
    bool set(int, const BallState& s) override { log.push_back("set"); b = s; return true; }
    bool step(double) override { log.push_back("step"); b.pos.x += 0.001; return true; }
    double fixedDelta() override { return 0.01; }
    bool castCue(int, Vec3, double) override { return false; }
    Contact cueContact(int) override { return Contact(); }
    bool inPocket(int) override { return false; }
    void park(int) override {}
    void prepare() override { log.push_back("prepare"); }
    void finish() override { log.push_back("finish"); }
};
int main() {
    H h; ShotIn S; S.cueId = 0; S.dx = 1; S.dz = 0; S.F = 1.0; Options O; O.maxTicks = 4; O.maxMs = 1000;
    Out R = predict(h, S, O);
    CHECK(R.restored, "restored"); int np = 0, nf = 0, firstStep = -1, lastSet = -1, finishAt = -1;
    for (size_t i = 0; i < h.log.size(); ++i) { if (h.log[i] == "prepare") ++np; if (h.log[i] == "finish") { ++nf; finishAt = (int)i; } if (h.log[i] == "step" && firstStep < 0) firstStep = (int)i; if (h.log[i] == "set") lastSet = (int)i; }
    CHECK(np == 1, "prepare once"); CHECK(nf == 1, "finish once"); CHECK(h.log.front() == "prepare", "prepare comes first (before any step)");
    CHECK(finishAt > lastSet, "finish is after the last restore set"); CHECK(h.log.back() == "finish", "finish is the last call");
    CHECK(h.b.pos.x == 0.1 && h.b.pos.z == 0.2, "ball back at its snapshot");
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
