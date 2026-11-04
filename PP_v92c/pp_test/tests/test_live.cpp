// P2Z: control-flow tests of pp2_live.hpp against a MOCK host (simple 2D balls + box cushions).  This tests snapshot/restore, the special path,
// first-contact ApplySpin, pocket parking, budget and failure handling -- NOT PhysX accuracy (that needs the game on a device).
#include "../native/live.hpp"
#include <cstdio>
#include <cstring>
using namespace pp2::live;
static int fails = 0, total = 0;
#define CHECK(c, msg) do { ++total; if (!(c)) { ++fails; printf("FAIL: %s  (%s)\n", msg, #c); } } while (0)

struct Mock : Host {
    std::vector<BallState> B; double R = 0.0275, W = 1.0, Hh = 0.5, e = 0.92, drag = 0.62; int failAfter = -1, stepsDone = 0; double pocketX = 99;
    bool slow = false; double fd = 1411199.0 / 141120000.0;
    int count() override { return (int)B.size(); }
    bool get(int i, BallState& s) override { s = B[i]; return true; }
    bool set(int i, const BallState& s) override { B[i] = s; return true; }
    double fixedDelta() override { return fd; }
    bool castCue(int ci, Vec3, double dist) override { if (castWall) return dist >= (W - R) - B[ci].pos.x - 1e-9; return castResult; }
    bool castResult = false, castWall = false;   // castWall: SphereCast along +x hits the cushion at x = W - R
    int castIdx = 0;
    bool step(double dt) override {
        if (failAfter >= 0 && stepsDone++ >= failAfter) return false;
        if (slow) { volatile double a = 0; for (long k = 0; k < 20000000; ++k) a += k; }
        for (auto& b : B) if (b.active) {
            b.vel.x *= std::max(0.0, 1 - drag * dt); b.vel.z *= std::max(0.0, 1 - drag * dt); b.pos.x += b.vel.x * dt; b.pos.z += b.vel.z * dt;
            if (b.pos.x > W - R) { b.pos.x = W - R; b.vel.x = -b.vel.x * e; } if (b.pos.x < -W + R) { b.pos.x = -W + R; b.vel.x = -b.vel.x * e; }
            if (b.pos.z > Hh - R) { b.pos.z = Hh - R; b.vel.z = -b.vel.z * e; } if (b.pos.z < -Hh + R) { b.pos.z = -Hh + R; b.vel.z = -b.vel.z * e; }
        }
        for (size_t i = 0; i < B.size(); ++i) for (size_t j = i + 1; j < B.size(); ++j) {
            if (!B[i].active || !B[j].active) continue; double dx = B[j].pos.x - B[i].pos.x, dz = B[j].pos.z - B[i].pos.z, d = std::sqrt(dx * dx + dz * dz);
            if (d < 2 * R && d > 1e-12) { double nx = dx / d, nz = dz / d, vn = (B[j].vel.x - B[i].vel.x) * nx + (B[j].vel.z - B[i].vel.z) * nz;
                if (vn < 0) { double jj = -(1 + e) * vn * 0.5; B[i].vel.x -= jj * nx; B[i].vel.z -= jj * nz; B[j].vel.x += jj * nx; B[j].vel.z += jj * nz; } }
        }
        return true;
    }
    Contact cueContact(int ci) override {
        Contact c; const BallState& a = B[ci];
        for (size_t j = 0; j < B.size(); ++j) if ((int)j != ci && B[j].active) { double dx = B[j].pos.x - a.pos.x, dz = B[j].pos.z - a.pos.z; if (std::sqrt(dx * dx + dz * dz) <= 2 * R + 1e-6) { c.touched = true; c.ballId = B[j].id; return c; } }
        if (std::fabs(a.pos.x) >= W - R - 1e-9 || std::fabs(a.pos.z) >= Hh - R - 1e-9) { c.touched = true; c.edge = true; }
        return c;
    }
    bool inPocket(int i) override { return B[i].active && B[i].pos.x > pocketX; }
    void park(int i) override { B[i].pos.y = -100; B[i].vel = {0, 0, 0}; B[i].active = false; }
    void add(int id, double x, double z) { BallState s; s.id = id; s.pos = {x, -0.0296, z}; B.push_back(s); }
};
static bool same(const std::vector<BallState>& a, const std::vector<BallState>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::memcmp(&a[i].pos, &b[i].pos, sizeof(Vec3)) || std::memcmp(&a[i].vel, &b[i].vel, sizeof(Vec3)) || std::memcmp(&a[i].ang, &b[i].ang, sizeof(Vec3)) || a[i].active != b[i].active) return false;
    return true;
}
int main() {
    { // 1. empty table, flag false: the special path ends exactly at baseline+vec (P2R: F=0.5 stops at 0.5), state restored bitwise
        Mock m; m.add(0, -0.5, 0.0); m.add(1, 0.9, 0.3); auto before = m.B; ShotIn s; s.cueId = 0; s.dx = 1; s.dz = 0; s.F = 0.5;
        Out r = predict(m, s); CHECK(r.ok, "ok"); CHECK(r.restored && same(before, m.B), "state restored bitwise");
        CHECK(!r.flag108 && !r.firstContact, "no flag/contact"); CHECK(r.frames.size() > 10, "frames recorded");
        double xend = r.frames.back().x[0]; CHECK(std::fabs(xend - 0.0) < 1e-6, "cue ends at baseline+vec (= 0.0)");
        CHECK(r.ticks < 800, "terminates");
    }
    { // 2. flag true -> 7 sub-steps per tick
        Mock m; m.castResult = true; m.add(0, -0.5, 0.0); ShotIn s; s.cueId = 0; s.dx = 1; s.F = 0.3; Options o; o.tmax = 0.05;
        Out r = predict(m, s, o); CHECK(r.ok && r.flag108, "flag108 true"); CHECK(r.steps == r.ticks * 7, "7 steps per tick");
    }
    { // 3. head-on contact: ApplySpin overrides both velocities once
        Mock m; m.add(0, -0.5, 0); m.add(1, 0.0, 0); ShotIn s; s.cueId = 0; s.dx = 1; s.F = 3.0; s.havePred = true; s.targetId = 1; s.spinValue = 0.7; s.spinUsed = false;
        s.collisionDir = {0, 0, 1}; s.targetDir = {1, 0, 0}; s.targetVel = 2.0; Options o; o.tmax = 0.6; auto before = m.B;
        Out r = predict(m, s, o); CHECK(r.ok && r.firstContact && r.applied, "contact + ApplySpin");
        CHECK(!r.events.empty() && r.events[0].what == "first contact ball 1", "event names ball 1");
        CHECK(same(before, m.B), "restored after contact");
        // target moved along +x only (velocity forced to (2,0,0) then drag): z unchanged
        double z1 = 0; for (auto& f : r.frames) z1 = f.z[1]; CHECK(std::fabs(z1) < 1e-9, "target keeps z");
        double x1 = r.frames.back().x[1]; CHECK(x1 > 0.3, "target travelled in +x");
        double zc = r.frames.back().z[0]; CHECK(zc > 0.05, "cue deflected to +z");
    }
    { // 4. contact with the WRONG ball: no override
        Mock m; m.add(0, -0.5, 0); m.add(1, 0.0, 0); m.add(2, 5.0, 5.0); ShotIn s; s.cueId = 0; s.dx = 1; s.F = 3.0; s.havePred = true; s.targetId = 2; s.targetVel = 2; s.spinValue = 1; s.targetDir = {1, 0, 0}; s.collisionDir = {0, 0, 1};
        Options o; o.tmax = 0.4; Out r = predict(m, s, o); CHECK(r.ok && r.firstContact && !r.applied, "no override for a different ball");
    }
    { // 5. edge first contact with an edge prediction
        Mock m; m.add(0, 0.9, 0); ShotIn s; s.cueId = 0; s.dx = 1; s.F = 3.0; s.havePred = true; s.predEdge = true; s.spinValue = 1.5; s.collisionDir = {-1, 0, 0};
        Options o; o.tmax = 0.8; Out r = predict(m, s, o); CHECK(r.ok && r.applied && r.firstContact, "edge ApplySpin"); CHECK(r.frames.back().x[0] < 0.9, "cue came back");
    }
    { // 6. pocket: ball parked, restored afterwards
        Mock m; m.pocketX = 0.2; m.add(0, -0.5, 0); m.add(1, 0.1, 0.0); auto before = m.B; ShotIn s; s.cueId = 0; s.dx = 1; s.F = 4; Options o; o.tmax = 1.0;
        Out r = predict(m, s, o); bool sawPocket = false; for (auto& e : r.events) if (e.what == "pocket") sawPocket = true; CHECK(sawPocket, "pocket event"); CHECK(same(before, m.B), "pocketed ball restored (active again)");
    }
    { // 7. time budget -> truncated, still restored
        Mock m; m.slow = true; m.add(0, -0.5, 0); auto before = m.B; ShotIn s; s.cueId = 0; s.dx = 1; s.F = 3; Options o; o.maxMs = 30;
        Out r = predict(m, s, o); CHECK(r.truncated, "truncated"); CHECK(same(before, m.B) && r.restored, "restored after truncation");
    }
    { // 8. Physics.Simulate failure -> ok=false, restored
        Mock m; m.failAfter = 3; m.add(0, -0.5, 0); auto before = m.B; ShotIn s; s.cueId = 0; s.dx = 1; s.F = 3; Out r = predict(m, s);
        CHECK(!r.ok && !r.err.empty(), "failure reported"); CHECK(same(before, m.B) && r.restored, "restored after failure");
    }
    { // 9. unknown cue id
        Mock m; m.add(5, 0, 0); ShotIn s; s.cueId = 0; Out r = predict(m, s); CHECK(!r.ok, "missing cue rejected");
    }
    { // 10. v86 fast-forward: same path as the full run, far fewer Physics.Simulate calls; verified against a real tick first
        auto run = [&](bool ff, double mockDrag, Mock*& keep) { static Mock m; m = Mock(); m.castWall = true; m.drag = mockDrag; m.add(0, -0.5, 0); m.add(1, 5, 5); ShotIn s; s.cueId = 0; s.dx = 1; s.F = 3.0; s.havePred = false;
            Options o; o.tmax = 1.2; o.fastForward = ff; keep = &m; return predict(m, s, o); };
        Mock* mk; Out off = run(false, 0.62, mk); auto before = mk->B; Out on = run(true, 0.62, mk);
        CHECK(off.ok && on.ok && off.firstContact && on.firstContact, "both runs reach the first contact");
        CHECK(on.ffTicks > 20 && on.ffMode == 0 && on.ffErr < 1e-9, "fast-forward engaged, damp-then-move matched the first real tick");
        CHECK(on.ffTail >= 0 && on.ffTail < 1e-9, "tail check after the skip agrees");
        CHECK(on.steps < off.steps / 3, "far fewer Physics.Simulate calls");
        CHECK(on.ticks == off.ticks, "same tick count");
        double dx = std::fabs(on.frames.back().x[0] - off.frames.back().x[0]), dz = std::fabs(on.frames.back().z[0] - off.frames.back().z[0]); CHECK(dx < 1e-9 && dz < 1e-9, "same final cue position");
        CHECK(on.events.size() == off.events.size() && !on.events.empty() && std::fabs(on.events[0].t - off.events[0].t) < 1e-9, "same first-contact time");
        CHECK(on.frames.size() == off.frames.size(), "same number of path frames");
        { double worst = 0; for (size_t i = 0; i < on.frames.size() && i < off.frames.size(); ++i) worst = std::max(worst, std::fabs(on.frames[i].x[0] - off.frames[i].x[0])); CHECK(worst < 1e-9, "every path frame identical"); }
        Out bad = run(true, 1.0, mk);   // a physics whose damping differs from the documented candidates: nothing may be skipped
        CHECK(bad.ok && bad.ffTicks == 0 && bad.ffMode == -1 && bad.ffErr > 3e-6, "mismatching physics: no fast-forward");
        Out nof = run(false, 1.0, mk); CHECK(bad.steps == nof.steps && bad.ticks == nof.ticks, "mismatch = identical to the full run");
        (void)before;
    }
    printf("%d checks, %d failed\n", total, fails); return fails ? 1 : 0;
}
