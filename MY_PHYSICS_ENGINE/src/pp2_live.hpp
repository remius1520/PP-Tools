// P2Z -- LIVE prediction on the game's OWN PhysX (item 7 of the master file).
// Idea: the game runs Physics.simulationMode = Script (P2P), so nothing steps the physics scene unless the game itself calls Physics.Simulate.
// While the player is aiming (balls at rest, GameManager.isHit == false => the game's collision/pocket handlers are guarded, P2U) we
//   1. snapshot every ball (pos / rot / linear / angular velocity / sleeping),
//   2. apply the game's own shot rules (P2Q/P2R special path + ApplySpin on first contact),
//   3. call the game's own Physics.Simulate(dt) tick by tick and record the paths,
//   4. restore the snapshot (always, RAII).
// The table colliders, PhysicsMaterials, ball Rigidbody values and the PhysX solver are the game's real ones => works for EVERY table with no tables data files.
// This header is engine-independent: all game access goes through `Host` (android/pp2_il2cpp_host.cpp = real game, tests/test_live.cpp = mock).
// NOT verified on a device.  Assumptions are listed in docs/FINDINGS_P2Z.md.
#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <chrono>
#include <algorithm>
namespace pp2 { namespace live {

struct Vec3 { double x = 0, y = 0, z = 0; };
struct BallState {
    int id = -1; Vec3 pos, vel, ang; double q[4] = {0, 0, 0, 1}; bool sleeping = false, active = true;
};
struct Contact { bool touched = false; bool edge = false; int ballId = -1; };   // first non-"Table" contact of the cue ball

// Everything the game gives us.  Indices are stable for one predict() call.
struct Host {
    virtual ~Host() {}
    virtual int count() = 0;
    virtual bool get(int i, BallState& s) = 0;               // Rigidbody position/rotation/linearVelocity/angularVelocity/IsSleeping
    virtual bool set(int i, const BallState& s) = 0;         // writes them back (and Sleep() when s.sleeping)
    virtual bool step(double dt) = 0;                        // Physics.Simulate(dt); false = failed
    virtual double fixedDelta() = 0;                         // 1411199/141120000 (P2I)
    virtual bool castCue(int cueIdx, Vec3 dir, double dist) = 0;   // ServerTracker +0x108: SphereCast(r + 2*defaultContactOffset, dist, mask 0xC00) hits something
    virtual Contact cueContact(int cueIdx) = 0;              // cue ball touching a non-"Table" collider now (Physics.OverlapSphere + tag)
    virtual bool inPocket(int idx) = 0;                      // ball sphere overlaps a "PocketInside" trigger (P2T: the game's rule is a trigger event)
    virtual void park(int idx) = 0;                          // take a pocketed ball out of the scene (move away + zero velocity); undone by restore
};

// The game's own client prediction (ShotData fields, read from PhysicsBuilder / CueBall by the host side), P2H.
struct ShotIn {
    int cueId = 0; double dx = 0, dz = 0, F = 0;             // forceDirection (xz); F = |forceDirection|
    bool havePred = false;                                    // false: no ApplySpin overrides, pure PhysX after the special path
    bool predEdge = false; int targetId = -1;                 // CueBall.collidedBall (+0x218): an Edge collider or a Ball
    double spinValue = -1; bool spinUsed = false;             // ShotData.spinValue / SpinApplied
    Vec3 collisionDir, targetDir; double targetVel = 0;       // ShotData.CollisionDirection / targetBallDirection / targetBallvelocity
};

struct Options {
    double tmax = 15.0, stop = 0.002; double substeps = 7.0; // ServerTracker.stopSimulateThreshold (special-path snap only) / substeps (P2Q)
    double ballStop = 0.002;                                  // Ball.sleepThreshold (+0xC4): Ball.IsMoving = |v| >= this decides the end of the shot (P2Z3)
    int maxTicks = 3000; double maxMs = 250.0;                // safety budget on the game's main thread
    bool recordEveryTick = true;
};
struct Frame { double t; std::vector<double> x, z; std::vector<char> alive; };
struct Event { double t; int ball; std::string what; double x, z; };
struct Out {
    bool ok = false, restored = false, truncated = false, flag108 = false, firstContact = false, applied = false; std::string err;
    double t = 0; std::vector<int> ids; std::vector<Frame> frames; std::vector<Event> events; int ticks = 0, steps = 0; double ms = 0;
};

namespace detail {
inline double len2(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
// RAII: puts every ball back (position, rotation, velocities, sleep) no matter how predict() leaves.
struct Guard {
    Host& h; std::vector<BallState> snap; bool armed = false, okRestore = true;
    explicit Guard(Host& host) : h(host) {}
    bool take() { int n = h.count(); snap.assign(n, BallState()); for (int i = 0; i < n; ++i) if (!h.get(i, snap[i])) return false; armed = true; return true; }
    void restore() { if (!armed) return; for (size_t i = 0; i < snap.size(); ++i) if (!h.set((int)i, snap[i])) okRestore = false; armed = false; }
    ~Guard() { restore(); }
};
}

// float32 like the game's special path (baseline +0xD4, vec +0xBC).
inline Out predict(Host& H, const ShotIn& S, const Options& O = Options()) {
    using clk = std::chrono::steady_clock; const auto t0 = clk::now(); Out R;
    auto ms = [&] { return std::chrono::duration<double, std::milli>(clk::now() - t0).count(); };
    detail::Guard G(H);
    if (!G.take()) { R.err = "snapshot failed"; return R; }
    const int n = (int)G.snap.size(); int ci = -1;
    for (int i = 0; i < n; ++i) { R.ids.push_back(G.snap[i].id); if (G.snap[i].id == S.cueId) ci = i; }
    if (ci < 0) { R.err = "cue ball not found"; return R; }
    const double fixedD = (double)(float)H.fixedDelta();
    const double m0 = std::sqrt(S.dx * S.dx + S.dz * S.dz);
    Vec3 u{m0 > 1e-5 ? S.dx / m0 : 0, 0, m0 > 1e-5 ? S.dz / m0 : 0};
    const float bx = (float)G.snap[ci].pos.x, bz = (float)G.snap[ci].pos.z, vcx = (float)(u.x * S.F), vcz = (float)(u.z * S.F);
    R.flag108 = H.castCue(ci, u, S.F);
    bool onSim = true, cueCollided = false, spinApplied = false; std::vector<char> alive(n, 1), gone(n, 0);
    for (int i = 0; i < n; ++i) alive[i] = G.snap[i].active ? 1 : 0;
    auto record = [&](double t) {
        if (!O.recordEveryTick) return; Frame f; f.t = t; f.x.resize(n); f.z.resize(n); f.alive = alive;
        for (int i = 0; i < n; ++i) { BallState b; if (H.get(i, b)) { f.x[i] = b.pos.x; f.z[i] = b.pos.z; } }
        R.frames.push_back(std::move(f)); };
    record(0.0);
    auto doStep = [&](double dt, double t) -> bool {   // one Physics.Simulate + the game's contact handlers (as we model them)
        if (!H.step(dt)) { R.err = "Physics.Simulate failed"; return false; } ++R.steps;
        if (!cueCollided) {
            Contact c = H.cueContact(ci);
            if (c.touched) {
                cueCollided = true; R.firstContact = true;       // CueBall.collided (+0x26C)
                BallState cb; H.get(ci, cb);
                R.events.push_back({t, S.cueId, c.edge ? "first contact edge" : "first contact ball " + std::to_string(c.ballId), cb.pos.x, cb.pos.z});
                const bool same = S.havePred && !spinApplied && (c.edge ? S.predEdge : (!S.predEdge && c.ballId == S.targetId));
                if (same) {   // CueBall.OnCollisionEnter -> ApplySpin (0x5DD83B4): overwrite velocities AFTER PhysX solved the contact
                    spinApplied = true; R.applied = true;
                    if (!c.edge) {
                        for (int i = 0; i < n; ++i) if (G.snap[i].id == c.ballId) {
                            BallState tb; if (H.get(i, tb)) { double l = detail::len2(S.targetDir); tb.vel = {l > 1e-5 ? S.targetDir.x / l * S.targetVel : 0, 0, l > 1e-5 ? S.targetDir.z / l * S.targetVel : 0}; H.set(i, tb); }
                        }
                    }
                    const double s = (!c.edge && S.spinValue <= 0.02 && S.spinUsed) ? 0.0 : S.spinValue; double l = detail::len2(S.collisionDir);
                    if (!c.edge) cb.ang = {0, 0, 0};
                    cb.vel = {l > 1e-5 ? S.collisionDir.x / l * s : 0, 0, l > 1e-5 ? S.collisionDir.z / l * s : 0}; H.set(ci, cb);
                }
            }
        }
        for (int i = 0; i < n; ++i) if (alive[i] && !gone[i] && H.inPocket(i)) {   // PocketCollision.OnCollisionOccuer: ball leaves play
            BallState b; H.get(i, b); alive[i] = 0; gone[i] = 1; R.events.push_back({t, G.snap[i].id, "pocket", b.pos.x, b.pos.z}); H.park(i);
            if (i == ci) cueCollided = true;
        }
        return true; };
    double t = 0;
    for (int tick = 0; tick < O.maxTicks && t < O.tmax; ++tick) {
        if (ms() > O.maxMs) { R.truncated = true; break; }
        int nSub = 1; double dtc = fixedD;
        if (onSim && !cueCollided && alive[ci]) {
            BallState cb; if (!H.get(ci, cb)) { R.err = "get cue failed"; break; }
            const float m = std::sqrt(vcx * vcx + vcz * vcz), ux = m > 1e-5f ? vcx / m : 0.f, uz = m > 1e-5f ? vcz / m : 0.f;
            const float tx = bx + ux * m, tz = bz + uz * m;
            cb.vel = {(double)tx - cb.pos.x, 0, (double)tz - cb.pos.z};
            if (R.flag108) { if (O.substeps <= 0) break; nSub = (int)std::lround(O.substeps); dtc = (double)(float)(fixedD / (double)(float)O.substeps); }   // game stalls when substeps<=0
            else if (detail::len2(cb.vel) < O.stop) { cb.pos.x = tx; cb.pos.z = tz; cb.vel = {0, 0, 0}; onSim = false; }
            H.set(ci, cb);
        }
        bool failed = false;
        for (int s = 0; s < nSub; ++s) if (!doStep(dtc, t + (s + 1) * dtc)) { failed = true; break; }
        if (failed) break;
        t += dtc * nSub; ++R.ticks; record(t);
        bool moving = false;
        for (int i = 0; i < n && !moving; ++i) if (alive[i]) { BallState b; if (H.get(i, b) && detail::len2(b.vel) >= O.ballStop) moving = true; }
        if (cueCollided) onSim = false;   // special path ends at the cue's first contact (P2R)
        if (!moving && !onSim) break;
    }
    R.t = t; R.ms = ms(); R.ok = R.err.empty(); G.restore(); R.restored = G.okRestore; return R;
}
}}  // namespace pp2::live
