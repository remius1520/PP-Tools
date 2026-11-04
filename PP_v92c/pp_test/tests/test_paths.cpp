// PP v84: frames -> thinned polylines (native/paths.hpp) + the live predictor's new accessors / strides (native/live.hpp)
#include "../native/paths.hpp"
#include <cstdio>
using namespace ppu; using namespace pp2::live;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s  (%s)\n", msg, #c); } } while (0)

static Out mk(int n) { Out R; for (int i = 0; i < n; ++i) R.ids.push_back(i); return R; }
static void addFrame(Out& R, double t, std::initializer_list<std::pair<double, double>> xz, std::initializer_list<int> alive = {}) {
    Frame f; f.t = t; for (auto& p : xz) { f.x.push_back(p.first); f.z.push_back(p.second); }
    if (alive.size()) for (int a : alive) f.alive.push_back((char)a); else f.alive.assign(f.x.size(), 1);
    R.frames.push_back(f);
}
struct Mock2 : Host {   // counts how often the cheap accessors are used
    std::vector<BallState> B; int posCalls = 0, speedCalls = 0, getCalls = 0, nearCalls = 0, pocketCalls = 0; bool farAlways = false;
    int count() override { return (int)B.size(); }
    bool get(int i, BallState& s) override { ++getCalls; s = B[i]; return true; }
    bool set(int i, const BallState& s) override { B[i] = s; return true; }
    bool step(double dt) override { for (auto& b : B) { b.pos.x += b.vel.x * dt; b.pos.z += b.vel.z * dt; b.vel.x *= (1 - 0.62 * dt); b.vel.z *= (1 - 0.62 * dt); } return true; }
    double fixedDelta() override { return 0.01; }
    bool castCue(int, Vec3, double) override { return false; }
    Contact cueContact(int) override { return Contact(); }
    bool inPocket(int) override { ++pocketCalls; return false; }
    void park(int) override {}
    bool pos(int i, double& x, double& z) override { ++posCalls; x = B[i].pos.x; z = B[i].pos.z; return true; }
    double speed(int i) override { ++speedCalls; return std::hypot(B[i].vel.x, B[i].vel.z); }
    bool nearPocket(double, double) override { ++nearCalls; return !farAlways; }
};

int main() {
    { // straight line collapses to 2 points, an L keeps its corner
        std::vector<P2> in, out; for (int i = 0; i <= 100; ++i) in.push_back({i * 0.01, 0.0}); rdp(in, 0.0025, out); CHECK(out.size() == 2, "straight line -> 2 points");
        in.clear(); for (int i = 0; i <= 50; ++i) in.push_back({i * 0.01, 0.0}); for (int i = 1; i <= 50; ++i) in.push_back({0.5, i * 0.01}); rdp(in, 0.0025, out);
        CHECK(out.size() == 3 && std::fabs(out[1].x - 0.5) < 1e-9 && std::fabs(out[1].z) < 1e-9, "L shape keeps the corner");
    }
    { // ball 0 moves, ball 1 stays, ball 2 is pocketed
        Out R = mk(3);
        addFrame(R, 0.00, {{0, 0}, {1, 1}, {2, 0}});
        addFrame(R, 0.01, {{0.1, 0}, {1, 1}, {2.1, 0}});
        addFrame(R, 0.02, {{0.2, 0}, {1, 1}, {2.2, 0}});
        addFrame(R, 0.03, {{0.3, 0}, {1, 1}, {0, 0}}, {1, 1, 0});   // ball 2 gone
        R.events.push_back({0.025, 2, "pocket", 2.25, 0.0});
        R.events.push_back({0.015, 0, "first contact ball 1", 0.15, 0.0});
        Scene S = buildScene(R);
        CHECK(S.paths.size() == 2, "unmoved ball has no path");
        bool p0 = false, p2 = false; for (auto& p : S.paths) { if (p.ball == 0) { p0 = true; CHECK(p.pts.size() == 2 && !p.pocketed, "ball 0: 2 points, not pocketed"); } if (p.ball == 2) { p2 = true; CHECK(p.pocketed && std::fabs(p.pts.back().x - 2.25) < 1e-9, "ball 2 ends at the pocket event"); } }
        CHECK(p0 && p2, "paths for balls 0 and 2");
        CHECK(S.marks.size() == 2, "two marks"); int k0 = 0, k1 = 0; for (auto& m : S.marks) (m.kind == 0 ? k0 : k1)++; CHECK(k0 == 1 && k1 == 1, "one contact + one pocket mark");
    }
    { // a ball that bounces: the corner survives thinning
        Out R = mk(1); double t = 0; for (int i = 0; i <= 20; ++i) { addFrame(R, t, {{i * 0.05, 0.0}}); t += 0.01; } for (int i = 1; i <= 20; ++i) { addFrame(R, t, {{1.0 - i * 0.05, 0.01 * i}}); t += 0.01; }
        Scene S = buildScene(R); CHECK(S.paths.size() == 1 && S.paths[0].pts.size() == 3, "bounce: start, corner, end");
    }
    { // live::predict with the cheap accessors and strides: same final state, far fewer game calls
        auto run = [&](int recEvery, int moveEvery, bool far, Mock2& m) { m.B.assign(2, BallState()); for (int i = 0; i < 2; ++i) { m.B[i].id = i; m.B[i].pos = {i * 0.5, -0.03, 0}; } m.farAlways = far;
            ShotIn s; s.cueId = 0; s.dx = 1; s.F = 0.3; Options o; o.recEvery = recEvery; o.moveEvery = moveEvery; return predict(m, s, o); };
        Mock2 a, b; Out ra = run(1, 1, false, a), rb = run(3, 8, true, b);
        CHECK(ra.ok && rb.ok, "both runs ok");
        CHECK(rb.frames.size() < ra.frames.size() / 2, "recEvery thins the frames");
        CHECK(std::fabs(rb.frames.back().t - rb.t) < 1e-9, "the last tick is always recorded");
        CHECK(b.pocketCalls == 0 && a.pocketCalls > 0, "nearPocket=false skips every OverlapSphere call");
        CHECK(b.speedCalls < a.speedCalls, "moveEvery thins the rest test");
        CHECK(std::fabs(rb.frames.back().x[0] - ra.frames.back().x[0]) < 1e-9, "same end position");
    }
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
