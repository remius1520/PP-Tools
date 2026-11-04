// v40 (this revision): exact geometry pass.
//  * combos: the target ALWAYS leaves along the exact line of centres at the real contact (2r). The game's own preview direction (CueBall.collidedBallForceDirection)
//    is computed with an inflated cast radius (r + 2*contactOffset) and is several degrees off on cuts; it is now only used to take the cue's post-hit RULE
//    (classic angle / spin yaw), re-expressed on the exact normal. It is ignored for a cue ball that already touched a cushion.
//  * banks: cushion normals from the probe are snapped to the axis when within ~3.5 degrees (straight rails reflect exactly, no per-bounce noise);
//    when the probe misses a wall that the measured table limits say is there, the rectangle wall is used as a safety net (no more balls leaving the table).
//  * the target's launch speed uses the game's own basis (v0leg - distFactor*dist), the same one the mover already used, instead of the drag-decayed speed.
//  * spin: with a spin yaw in charge the cue's post-hit speed is not crushed to ~4% on a near head-on hit (spinSinFloor).
// PremiumPath core v37: TIME-DOMAIN simulation of ALL balls (pure C++, no Android/Unity dependency).
// Plane = Unity (x,z).
//
// What changed vs v36 (ray-after-ray, other balls frozen):
//  * every ball that moves is simulated together; the next event (ball-ball | cushion | pocket | rest) decides what happens
//    -> combos, breaks, balls that get hit while still rolling, balls hit twice.
//  * motion = Unity linear drag:  v(t) = v0*exp(-drag*t),  speed vs distance  v(s) = v0 - drag*s,  stopping distance v0/drag.
//  * cushion  (PhysicsSolver.BallToEdgeCollisionDir):   v' = reflect(v, n) * edgerestitution (0.92)
//  * ball-ball, target at rest (PhysicsBuilder.PredictedFinalVelocity / BallToBallCollisionDir):
//        target : direction = line of centres, speed = ballrestituation(0.92) * |cos(cut)| * v_in
//        mover  : direction = tangent, speed = clamp((v0leg - distFactor*dist) * clamp(|sin(cut)|, minCueSin, 1), 0, 20)   [gameCueSpeed]
//    both balls moving (combo): equal-mass impulse along the line of centres with transfer coefficient ballrestituation.
//  * the spin model of v36 (follow / draw / side) is REMOVED: it was a guess and does not exist in the game (spin rotates the DIRECTION through a
//    Quaternion, see HANDOFF). Spin hooks are kept as Config fields (spinX / spinY, legacy kSide.. = no-ops) so native/payload.cpp still compiles;
//    phase 3 will fill them in.
//  * pockets: capture circle (mouth) + cushion-gap rule (as v20) or the game's real geometry through a Probe (Physics.SphereCast).
#pragma once
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <limits>

namespace pp {

struct V2 { double x = 0, z = 0; };
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.z + b.z}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.z - b.z}; }
inline V2 operator*(V2 a, double s) { return {a.x * s, a.z * s}; }
inline double dot(V2 a, V2 b) { return a.x * b.x + a.z * b.z; }
inline double len(V2 a) { return std::sqrt(dot(a, a)); }

struct BallIn { int id; V2 p; double r; };

struct Pocket { V2 p; double mouth; double gap; bool corner; double gapZW = -1, gapXW = -1; };   // mouth = capture circle radius, gap = half width of the cushion opening along the wall; v84: gapZW / gapXW = the MEASURED opening along a z-wall (x extent) / x-wall (z extent), -1 = use gap

struct Table {
    double xmin, xmax, zmin, zmax;      // limits for the BALL CENTRE (cushion face already moved in by the ball radius)
    bool measured = false;              // v40: limits come from the real colliders (not the guessed inset) -> usable as a safety net behind the probe
    std::vector<Pocket> pockets;

    static Table build(const std::vector<V2>& pk, const double* lim, double inset, double cornerMouth, double midMouth, double cornerGap, double midGap) {
        Table t; double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (auto& q : pk) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); z0 = std::min(z0, q.z); z1 = std::max(z1, q.z); }
        if (lim) { t.xmin = lim[0]; t.xmax = lim[1]; t.zmin = lim[2]; t.zmax = lim[3]; t.measured = true; }
        else { t.xmin = x0 + inset; t.xmax = x1 - inset; t.zmin = z0 + inset; t.zmax = z1 - inset; }
        double cx = (x0 + x1) * 0.5;
        for (auto& q : pk) { bool corner = std::fabs(q.x - cx) >= 0.3 * (x1 - x0); t.pockets.push_back({q, corner ? cornerMouth : midMouth, corner ? cornerGap : midGap, corner}); }
        return t;
    }
    static Table fromPockets(const std::vector<V2>& pk, double inset, double cornerMouth, double midMouth, double cornerGap = 0.11, double midGap = 0.07) {
        return build(pk, nullptr, inset, cornerMouth, midMouth, cornerGap, midGap);
    }
    static Table fromLimits(const std::vector<V2>& pk, double xmin, double xmax, double zmin, double zmax, double cornerMouth, double midMouth, double cornerGap = 0.11, double midGap = 0.07) {
        double l[4] = {xmin, xmax, zmin, zmax}; return build(pk, l, 0, cornerMouth, midMouth, cornerGap, midGap);
    }
    bool inside(V2 q, double m) const { return q.x > xmin - m && q.x < xmax + m && q.z > zmin - m && q.z < zmax + m; }
    // axis 1 = wall x=lim, axis 2 = wall z=lim; hit = crossing point of the ray (origin, dir) with the wall line.
    // Returns the pocket whose opening the crossing falls into, or -1:
    //  (a) the crossing lies within the cushion gap along the wall, or
    //  (b) the ray is heading at the pocket and passes within the mouth radius of its centre.
    int gapPocket(int axis, double lim, V2 hit, V2 origin, V2 dir) const {
        for (size_t i = 0; i < pockets.size(); ++i) {
            const Pocket& k = pockets[i];
            if (axis == 2) { if (std::fabs(k.p.z - lim) > 0.5 * (zmax - zmin)) continue; }
            else { if (!k.corner || std::fabs(k.p.x - lim) > 0.5 * (xmax - xmin)) continue; }
            double along = axis == 2 ? std::fabs(hit.x - k.p.x) : std::fabs(hit.z - k.p.z);
            const double gw = axis == 2 ? (k.gapZW > 0 ? k.gapZW : k.gap) : (k.gapXW > 0 ? k.gapXW : k.gap);   // v84
            if (along < gw) return (int)i;
            V2 w = k.p - origin; double s = dot(w, dir);
            if (s > 0 && std::fabs(w.x * dir.z - w.z * dir.x) < k.mouth && len(hit - k.p) < 0.3) return (int)i;
        }
        return -1;
    }
};

struct Config {
    double drag1 = 0;            // v69b: drag of the CUE ball on its FIRST leg (before it touches anything; the stick launch = sliding phase). 0 = same as drag. Measured: first-leg stop distance = v0/1.1..1.19, but the tails after collisions decay at 0.5-0.67
    double drag = 0.62;          // 1/s -> stopping distance v0/drag
    double vMax = 4.3;           // launch speed at power 1.0 (= PhysicsBuilder.levelMaxPower)
    double cushionKeep = 0.92;   // PhysicsSolver.edgerestitution
    double ballKeep = 0.92;      // PhysicsSolver.ballrestituation
    int maxBounces = 6;          // cushion bounces RECORDED per track (display limit; the physics goes on, see Result::finals)
    int maxChain = 8;            // track generations recorded (display limit)
    double minSpeed = 0.03;      // below this a ball is at rest (it still creeps v/drag further: that is added)
    // --- ball-ball model (PhysicsBuilder.PredictedFinalVelocity) ---
    bool gameCueSpeed = true;    // mover speed after hitting a ball at rest = clamp((v0leg - distFactor*dist) * clamp(|sin|, minCueSin, 1), 0, 20)
    double distFactor = 1.0;     // game uses 1.0 per unit of distance (NOT the drag): confirmed on 4 shots
    double minCueSin = 0.04;     // PhysicsBuilder+0x4C minCueAngelRadiuse
    double speedCap = 20.0;
    // --- integration ---
    double dt = 1.0 / 240.0;     // max time step; events are located inside a step by solving for the contact
    double tMax = 25.0;
    double budgetMs = 0;         // v37h: wall-clock cap for one simulate() call (0 = unlimited). The payload sets it so a 15-ball rack can never freeze a frame.
    // --- legacy spin fields (v36); the model is gone, kept so the payload still compiles. spinX / spinY reserved for phase 3.
    double spinX = 0, spinY = 0;
    double kSide = 0, kFollow = 0, kDraw = 0, spinDecay = 0;
    // v38 cushion spin, decoded from the game binary (analysis/game_asm):
    //   CueBall.OnHandleCueDirection (0x5DD6EE0): Qspin = Quaternion.Euler(0, SpinController.SpinAngelDeg, 0)   (degrees, exact, no scale)
    //   edge:  dirOut = normalize( Qspin * ( v - 2(v.n)n ) ), y forced 0, NO restitution inside (PhysicsBuilder.PredictCueDirAfterEdgeCollision 0x5D43858)
    //   Unity is left handed: Euler(0,t,0) maps (x,z) -> (x cos t + z sin t, z cos t - x sin t). Applied to the CUE ball's FIRST cushion contact only,
    //   because the game computes this once for the first contact of the aim line (-> CueBall.forceDirectionAfterCollided @0x220).
    double edgeSpinDeg = 0; int edgeSpinBounces = 1;
    // --- v65 (phase 1: real PhysX numbers read from the game) ---
    // PhysX applies restitution only when the approach speed along the contact normal is ABOVE Physics.bounceThreshold; below it the normal part is
    // killed (the ball slides along the cushion). bounceThreshold = the live Physics.bounceThreshold (0 = off / unknown), lowBounceKeep = normal retention below it (PhysX: 0).
    double bounceThreshold = 0, lowBounceKeep = 0;
    double cueKeep = 1.0;        // v64i: multiplier on the mover's speed after a ball hit (learned on the device from clean cut shots)
    double tanKeep = -1;         // v64h: tangential retention at a cushion (normal part keeps cushionKeep). <=0 = same as cushionKeep (old uniform behaviour). Learned on the device from clean one-bounce shots.
    // the game's OWN first-contact result (CueBall.collidedBallForceDirection / forceDirectionAfterCollided): used for the first cue contact if it
    // agrees with the line of centres (within ~20 deg)
    bool hasOv = false; V2 ovTarget, ovCue;
    // --- v40 ---
    bool ovExactGeometry = true; // target direction = exact line of centres; the game's cue rule is rotated onto the exact normal (see header)
    bool ovPristineOnly = true;  // the game's first-contact data belongs to the aim line: only valid for a cue ball that has not touched a cushion yet
    bool snapNormals = true;     // probe normals within snapTol of an axis are snapped to it (straight rails)
    double snapTol = 0.06;       // |component| below this = axis aligned (0.06 ~ 3.4 deg)
    bool wallNet = true;         // measured limits catch a wall the probe missed
    bool tgtGameBasis = false;   // target speed = ballKeep*|cos|*min(v, v0leg - distFactor*dist)
    bool liveSpeedBasis = true;  // v71d (SolveCollisions 0x5D3D35C calls BallToBallCollisionDir with hitdistance=0): only the cue's FIRST clean contact uses the preview basis (v0leg - distFactor*dist); every other ball-ball contact (after a cushion, moving balls, combos) uses the ball's CURRENT speed.
    double cueEdgeKeepN = -1;    // v71e: cue-ball (PhysX) cushion retention ALONG the normal above the bounce threshold, measured live per collision by the payload ([23] CU lines). <=0 = use cushionKeep.
    double edgeRestitution = 0.92; // v71e: PhysicsSolver.edgerestitution (decoded, libil2cpp .cctor 0x5D3A974): the scripted cushion factor of every NON-cue ball
    int liveTgtModel = 0;        // v71e: target launch factor for NON-scripted ball contacts (not the cue's first clean hit). 0 = cos(cut) (physical, old), 1 = 1.0 (game: |sin(angle between the ball's post-PhysX velocity and the normal)| ~ 1), 2 = sin(cut). Chosen AUTOMATICALLY by the payload from the live [22] BB measurements.
    bool scriptedEdgeNonCue = true; // v71c (decoded from libil2cpp, PhysicsBuilder.SolveCollisions 0x5D3D35C): a NON-cue ball's cushion hit is always scripted = reflect(v,n)*edgerestitution (0.92), no tangential loss, no bounce threshold. Only the cue ball (unspun) is left to PhysX, so tanKeep / bounceThreshold (learned from cue shots) must NOT touch other balls.
    // v72 (decoded: ServerTracker.OnServerSimulate 0x5DB856C = Physics.Simulate(fixedDeltaTime / substeps) x substeps): PhysX damps in DISCRETE steps, v *= (1 - drag*dt), and moves with the damped speed.
    // Total stopping distance is therefore v0*(1-drag*dt)/drag, not v0/drag. stepDt > 0 converts the RAW game drag to the continuous rate that gives the same distance: drag' = drag/(1 - drag*dt).
    // Use it ONLY with the game's raw Rigidbody drag (physfeed); a drag learned from real stops (stopdrag) already contains this effect. 0 = off.
    double stepDt = 0;
    bool spinActive = false;     // a spin yaw is in charge of the cue's post-hit direction
    double spinSinFloor = 0.30;  // floor of the |sin(cut)| factor of the cue's post-hit speed while spinActive (provisional: fit from spin shots)
};

enum End { END_REST = 0, END_POCKET = 1, END_BALL = 2, END_BOUNCELIMIT = 3 };

struct Track {
    int id = -1;                     // -1 = cue ball
    int parent = -2;
    int gen = 0;
    std::vector<V2> pts;             // start, every bounce, end
    std::vector<int> bounceIdx;      // indices in pts that are cushion bounces
    End end = END_REST;
    int hitId = -2;                  // ball hit at the end (when END_BALL)
    int pocket = -1;                 // pocket index (when END_POCKET)
    V2 ghost;                        // centre of the moving ball at contact
    double t0 = 0, t1 = 0;           // start / end time (s)
};

struct Final { int id; V2 p; bool pocketed; int pocket; double tRest; };   // where every simulated ball ends up

struct Result { std::vector<Track> tracks; std::vector<Final> finals; };

// ---- helpers ----
inline double rayCircle(V2 p, V2 d, V2 q, double R) {
    V2 w = p - q; double b = dot(d, w), c = dot(w, w) - R * R;
    if (c <= 0 && b >= 0) return 1e18;          // overlapping and moving apart
    double disc = b * b - c; if (disc < 0) return 1e18;
    double t = -b - std::sqrt(disc);
    return t >= 1e-9 ? t : (c <= 0 ? 0.0 : 1e18);
}

// v36: GAME GEOMETRY. A Probe answers "where does a sphere of radius r, starting at p and moving along d, first touch the cushions?" (Physics.SphereCast
// against the real cushion MeshColliders). Without a probe the rectangle limits of the Table are used.
struct CastHit { double t = 0; V2 n; };            // n = surface normal (x,z), pointing from the wall toward the ball
struct Probe { virtual ~Probe() {} virtual bool cast(V2 p, V2 d, double r, double maxDist, CastHit& out) const = 0; };

class Sim {
public:
    void setProbe(const Probe* pr) { probe_ = pr; }
    Sim(const Table& T, const Config& C, const std::vector<BallIn>& balls, V2 cue, double cueR) : T_(T), C_(C) {
        for (auto& b : balls) if (T_.inside(b.p, 0.06)) add(b.id, b.p, b.r);   // balls in the tray (outside the table) are ignored
        add(-1, cue, cueR); cueIdx_ = (int)B_.size() - 1;
    }

    // dir = unit vector of the aim, power 0..1 (speed = power * vMax)
    Result run(V2 dir, double power) { return runSpeed(dir, power * C_.vMax); }
    Result runSpeed(V2 dir, double speed) {
        Result R; B& c = B_[cueIdx_];
        double l = len(dir); if (l > 0) dir = dir * (1.0 / l);
        c.d = dir; c.v = speed; c.v0leg = speed; c.distLeg = 0; c.moving = speed > C_.minSpeed;
        c.trk = newTrack(R, c, -1, -2, 0, 0.0);
        simulate(R);
        for (auto& b : B_) R.finals.push_back({b.id, b.p, b.out, b.pocket, b.tRest});
        return R;
    }

private:
    struct B {
        int id; V2 p; double r; V2 d{1, 0}; double v = 0; bool moving = false, out = false; int pocket = -1; double tRest = 0;
        double v0leg = 0, distLeg = 0;            // speed at the start of the current leg and distance since then (PredictedFinalVelocity inputs)
        int trk = -1; int gen = 0; bool rec = true; int bounces = 0; int spinHits = 0; bool clean = true;   // clean = no cushion contact yet
        // next static event on the current leg
        double sEvt = 1e18; int kind = 0; int idx = -1; CastHit ch;   // kind 1 x-wall, 2 z-wall, 4 pocket, 5 probe cushion
    };
    Table T_; Config C_; std::vector<B> B_; int cueIdx_ = -1; const Probe* probe_ = nullptr;
    double discK(double k) const { return (C_.stepDt > 0 && k > 0 && k * C_.stepDt < 0.9) ? k / (1.0 - k * C_.stepDt) : k; }   // v72
    double kOf(const B& b) const { return discK((C_.drag1 > 0 && b.id == -1 && b.gen == 0) ? C_.drag1 : C_.drag); }   // v69b: per-ball drag (cue ball's first leg vs everything else)

    void add(int id, V2 p, double r) { B b; b.id = id; b.p = p; b.r = r; B_.push_back(b); }

    int newTrack(Result& R, B& b, int id, int parent, int gen, double t) {
        R.tracks.emplace_back(); Track& k = R.tracks.back();
        k.id = id; k.parent = parent; k.gen = gen; k.pts.push_back(b.p); k.t0 = t; k.t1 = t;
        b.gen = gen; b.rec = gen <= C_.maxChain; b.bounces = 0;
        return (int)R.tracks.size() - 1;
    }
    Track* tr(Result& R, B& b) { return (b.rec && b.trk >= 0) ? &R.tracks[b.trk] : nullptr; }

    // plan the next cushion / pocket event of a moving ball along its current direction
    void plan(B& b) {
        b.sEvt = 1e18; b.kind = 0; b.idx = -1;
        double reach = b.v / kOf(b); if (reach < 1e-9) return;
        double best = reach;
        V2 p = b.p, d = b.d;
        if (probe_) {
            CastHit pc;
            if (probe_->cast(p, d, b.r, best, pc) && pc.t > 1e-9 && pc.t < best) { best = pc.t; b.kind = 5; b.ch = pc; }
            if (C_.wallNet && T_.measured) {        // v40: the probe missed (or overshot) a wall that the measured limits put closer -> do not let the ball leave the table
                auto net = [&](double pos, double dv, double lim, int kind) {
                    if (std::fabs(dv) < 1e-12) return;
                    double t = (lim - pos) / dv;
                    if (!(t > 1e-9 && t < best - 0.004)) return;
                    if (T_.gapPocket(kind, lim, p + d * t, p, d) >= 0) return;      // an opening: the pocket circles decide
                    best = t; b.kind = kind; b.idx = -1;
                };
                net(p.x, d.x, d.x > 0 ? T_.xmax : T_.xmin, 1); net(p.z, d.z, d.z > 0 ? T_.zmax : T_.zmin, 2);
            }
        } else {
            auto wall = [&](double pos, double dv, double lim, int kind) {
                if (std::fabs(dv) < 1e-12) return;
                double t = (lim - pos) / dv;
                if (!(t > 1e-9 && t < best)) return;
                int gp = T_.gapPocket(kind, lim, p + d * t, p, d);
                best = t; if (gp >= 0) { b.kind = 4; b.idx = gp; } else { b.kind = kind; b.idx = -1; }
            };
            wall(p.x, d.x, d.x > 0 ? T_.xmax : T_.xmin, 1); wall(p.z, d.z, d.z > 0 ? T_.zmax : T_.zmin, 2);
        }
        for (size_t i = 0; i < T_.pockets.size(); ++i) {
            V2 w = p - T_.pockets[i].p; double m = T_.pockets[i].mouth;
            double t = dot(w, w) <= m * m ? 0.0 : rayCircle(p, d, T_.pockets[i].p, m);
            if (t < best) { best = t; b.kind = 4; b.idx = (int)i; }
        }
        if (b.kind != 0) b.sEvt = best;
    }

    double timeForDist(const B& b, double s) const {          // time for the ball to travel s with v(s)=v-drag*s; inf if it stops first
        double k = kOf(b); double f = k * s / b.v;
        if (!(f < 1.0 - 1e-12)) return std::numeric_limits<double>::infinity();
        return -std::log(1.0 - f) / k;
    }
    void advance(B& b, double h) {                            // exponential drag over h seconds
        if (!b.moving) return;
        double k = kOf(b), e = std::exp(-k * h), s = b.v / k * (1.0 - e);
        b.p = b.p + b.d * s; b.v *= e; b.distLeg += s;
        if (b.sEvt < 1e17) b.sEvt = std::max(0.0, b.sEvt - s);
    }
    void restBall(Result& R, B& b, double t) {
        // the ball still creeps v/drag: add it (clamped to the free distance of the planned event, if any)
        double s = b.v / kOf(b); if (b.sEvt < s) s = b.sEvt;
        if (s > 0) b.p = b.p + b.d * s;
        b.v = 0; b.moving = false; b.tRest = t;
        if (Track* k = tr(R, b)) { if (len(k->pts.back() - b.p) > 1e-9) k->pts.push_back(b.p); k->end = END_REST; k->t1 = t; }
    }

    // closing contact between two balls inside the step [0,h]; returns tau in [0,1] of the step or -1
    double pairContact(const B& a, const B& c, double h) const {   // displacement of each ball over the step: v * (1 - exp(-k*h)) / k with ITS OWN k
        V2 da{0, 0}, dc{0, 0};
        if (a.moving) { double k = kOf(a); da = a.d * (a.v * (1.0 - std::exp(-k * h)) / k); }
        if (c.moving) { double k = kOf(c); dc = c.d * (c.v * (1.0 - std::exp(-k * h)) / k); }
        V2 P = c.p - a.p, D = dc - da; double R = a.r + c.r;
        { double reach = len(da) + len(dc) + R; if (P.x * P.x + P.z * P.z > reach * reach) return -1.0; }   // v40: cannot touch inside this step
        double A = dot(D, D), Bq = 2.0 * dot(P, D), Cq = dot(P, P) - R * R;
        if (Cq <= 0) return Bq < 0 ? 0.0 : -1.0;
        if (A < 1e-18 || Bq >= 0) return -1.0;
        double disc = Bq * Bq - 4.0 * A * Cq; if (disc < 0) return -1.0;
        double tau = (-Bq - std::sqrt(disc)) / (2.0 * A);
        return (tau >= 0.0 && tau <= 1.0) ? tau : -1.0;
    }

    void bumpTrack(Result& R, B& b, End e, double t) { if (Track* k = tr(R, b)) { k->end = e; k->t1 = t; } }

    void ballBall(Result& R, int ia, int ic, double t) {
        B& a = B_[ia]; B& c = B_[ic];
        V2 n = c.p - a.p; double nl = len(n); if (nl < 1e-12) return; n = n * (1.0 / nl);
        V2 va = a.d * (a.moving ? a.v : 0.0), vc = c.d * (c.moving ? c.v : 0.0);
        double un = dot(va - vc, n); if (un <= 0) return;      // not closing
        bool staticTarget = !c.moving;
        // close the tracks of both balls at the contact
        V2 ghostA = a.p, ghostC = c.p;
        int genA = a.gen, genC = c.gen;
        if (Track* k = tr(R, a)) { if (len(k->pts.back() - a.p) > 1e-9) k->pts.push_back(a.p); k->end = END_BALL; k->hitId = c.id; k->ghost = ghostA; k->t1 = t; }
        if (!staticTarget) if (Track* k = tr(R, c)) { if (len(k->pts.back() - c.p) > 1e-9) k->pts.push_back(c.p); k->end = END_BALL; k->hitId = a.id; k->ghost = ghostC; k->t1 = t; }
        V2 na, nc; double sa, sc;
        if (staticTarget) {
            double cs = std::max(0.0, dot(a.d, n)), sn = std::sqrt(std::max(0.0, 1.0 - cs * cs));
            V2 tn = n, tdir;
            V2 tang = a.d - n * cs; double tl = len(tang);
            tdir = tl > 1e-6 ? tang * (1.0 / tl) : a.d;
            double gl = len(C_.ovTarget);
            bool ov = C_.hasOv && a.id == -1 && genA == 0 && (a.clean || !C_.ovPristineOnly) && gl > 0.5 && dot(C_.ovTarget, n) > 0.94 * gl;
            if (ov) {
                double l2 = len(C_.ovCue);
                if (C_.ovExactGeometry) {
                    // v40: the game's numbers come from its inflated cast; keep only the RULE it applies to the cue (cue-after = yaw * normal) and re-express it on the exact normal
                    V2 g = C_.ovTarget * (1.0 / gl); double c = dot(g, n), sg = g.x * n.z - g.z * n.x;
                    if (l2 > 0.5) { V2 u = C_.ovCue * (1.0 / l2); tdir = V2{u.x * c - u.z * sg, u.x * sg + u.z * c}; }
                } else { tn = C_.ovTarget * (1.0 / gl); if (l2 > 0.5) tdir = C_.ovCue * (1.0 / l2); }
            }
            const bool scriptedHit = a.id == -1 && genA == 0 && a.clean;   // v71d
            const bool preview = !C_.liveSpeedBasis || scriptedHit;
            double basis = a.v;                                    // v40: speed the game feeds to BOTH balls = launch force minus the distance rolled (1.0 per unit, not the drag)
            if (preview && C_.gameCueSpeed && C_.tgtGameBasis) basis = std::min(a.v, std::max(0.0, a.v0leg - C_.distFactor * a.distLeg));
            double tf = cs; if (!preview) { if (C_.liveTgtModel == 1) tf = 1.0; else if (C_.liveTgtModel == 2) tf = sn; }   // v71e
            sc = C_.ballKeep * tf * basis; nc = tn;
            double ms = std::min(1.0, std::max(C_.minCueSin, sn));
            if (C_.spinActive && a.id == -1 && genA == 0) ms = std::max(ms, C_.spinSinFloor);
            sa = (C_.gameCueSpeed && preview) ? std::min(C_.speedCap, std::max(0.0, a.v0leg - C_.distFactor * a.distLeg)) * ms : a.v * ms;
            sa *= C_.cueKeep;
            if (sa > a.v) sa = a.v;                                // a ball never gains speed from the contact
            na = tdir;
            // v66: the game applies the spin yaw ONCE, at the cue's first contact. When that contact is a ball and the game's own cue rule (ovCue, spin yaw included) is used,
            // the yaw is already inside 'na' -> mark it consumed so the first cushion afterwards does not rotate the cue a second time (shot 5: ovC already turned ~43 deg, the real
            // cue came straight back off the rail while the line bent a further 47 deg).
            if (ov && a.id == -1 && genA == 0 && len(C_.ovCue) > 0.5 && C_.edgeSpinDeg != 0) a.spinHits = C_.edgeSpinBounces;
        } else {
            V2 va2 = va - n * (C_.ballKeep * un), vc2 = vc + n * (C_.ballKeep * un);
            sa = len(va2); sc = len(vc2); na = sa > 1e-12 ? va2 * (1.0 / sa) : a.d; nc = sc > 1e-12 ? vc2 * (1.0 / sc) : c.d;
        }
        a.d = na; a.v = sa; a.v0leg = sa; a.distLeg = 0; a.moving = sa > C_.minSpeed; a.sEvt = 1e18; a.kind = 0;
        c.d = nc; c.v = sc; c.v0leg = sc; c.distLeg = 0; c.moving = sc > C_.minSpeed; c.sEvt = 1e18; c.kind = 0;
        // new tracks (legs)
        int pa = a.id, pc = a.id;
        if (a.moving) { a.trk = newTrack(R, a, a.id, -1, genA + 1, t); R.tracks[a.trk].ghost = ghostA; } else { a.tRest = t; a.trk = newTrack(R, a, a.id, -1, genA + 1, t); R.tracks[a.trk].end = END_REST; }
        int ng = std::max(genA, staticTarget ? genA : genC) + 1;
        c.trk = newTrack(R, c, c.id, pc, ng, t); if (!c.moving) { c.tRest = t; R.tracks[c.trk].end = END_REST; }
        (void)pa;
        if (a.moving) plan(a);
        if (c.moving) plan(c);
    }

    void cushion(Result& R, B& b, double t) {
        // the ball has been advanced to the contact
        V2 q = b.p; double vn; V2 nOut{0, 0}; (void)nOut; const int kindBefore = b.kind;
        if (b.kind == 5) {
            V2 n = b.ch.n; double nl = len(n);
            if (nl < 0.3) { restBallAt(R, b, t); return; }       // hit a face that is not a vertical wall: stop on it
            n = n * (1.0 / nl); if (dot(b.d, n) > 0) n = n * -1.0;
            if (C_.snapNormals) {                                // v40: straight rails reflect EXACTLY (mesh normals carry noise at facets / vertices)
                if (std::fabs(n.z) < C_.snapTol) n = V2{n.x > 0 ? 1.0 : -1.0, 0.0};
                else if (std::fabs(n.x) < C_.snapTol) n = V2{0.0, n.z > 0 ? 1.0 : -1.0};
                if (dot(b.d, n) > 0) n = n * -1.0;
            }
            vn = -dot(b.d, n);
            b.d = b.d - n * (2.0 * dot(b.d, n)); double dl = len(b.d); b.d = b.d * (1.0 / dl);
            b.p = q + b.d * 1e-4;                                // v84: restart the next cast 0.1 mm ALONG the new direction (was 0.1 mm along the normal = a 0.1 mm sideways shift of the whole path at every bounce); the face we are leaving is skipped by the probe
        } else if (b.kind == 1) { vn = std::fabs(b.d.x); b.d.x = -b.d.x; q.x = b.d.x > 0 ? T_.xmin : T_.xmax; b.p = q; }
        else { vn = std::fabs(b.d.z); b.d.z = -b.d.z; q.z = b.d.z > 0 ? T_.zmin : T_.zmax; b.p = q; }
        (void)vn; b.clean = false;
        const bool scriptedEdge = C_.scriptedEdgeNonCue && b.id != -1;   // v71c
        const double keepN0 = (!scriptedEdge && b.id == -1 && C_.cueEdgeKeepN > 0.0) ? C_.cueEdgeKeepN : C_.cushionKeep;   // v71e
        double speedF = scriptedEdge ? C_.edgeRestitution : keepN0;
        const bool tanDiffers = (C_.tanKeep > 0.0 && std::fabs(C_.tanKeep - keepN0) > 1e-6) || std::fabs(keepN0 - C_.cushionKeep) > 1e-6;
        if (!scriptedEdge && (tanDiffers || C_.bounceThreshold > 0.0)) {   // v64h: different retention along / across the wall (PhysX friction); v65: PhysX bounce threshold
            V2 nA{0, 0};
            if (kindBefore == 5) { V2 n0 = b.ch.n; double l0 = len(n0); if (l0 > 0.3) { nA = n0 * (1.0 / l0); if (dot(b.d, nA) < 0) nA = nA * -1.0; if (C_.snapNormals) { if (std::fabs(nA.z) < C_.snapTol) nA = V2{nA.x > 0 ? 1.0 : -1.0, 0.0}; else if (std::fabs(nA.x) < C_.snapTol) nA = V2{0.0, nA.z > 0 ? 1.0 : -1.0}; } } }
            else if (kindBefore == 1) nA = V2{b.d.x > 0 ? 1.0 : -1.0, 0.0};
            else nA = V2{0.0, b.d.z > 0 ? 1.0 : -1.0};
            if (len(nA) > 0.5) {
                double dn = dot(b.d, nA); V2 dt = b.d - nA * dn;
                if (dn > 0.0) {
                    const bool lowB = C_.bounceThreshold > 0.0 && b.v * dn < C_.bounceThreshold;      // approach speed along the normal (the speed is still the pre-bounce one here)
                    const double eN = lowB ? C_.lowBounceKeep : keepN0, eT = C_.tanKeep > 0.0 ? C_.tanKeep : keepN0;
                    if (tanDiffers || lowB) { V2 o = nA * (eN * dn) + dt * eT; double f = len(o); if (f > 1e-9) { b.d = o * (1.0 / f); speedF = f; } else speedF = 0.0; }
                }
            }
        }
        applyEdgeSpin(b, kindBefore);
        if (Track* k = tr(R, b)) {
            k->pts.push_back(q);
            if (b.bounces >= C_.maxBounces) { k->end = END_BOUNCELIMIT; k->t1 = t; b.rec = false; }
            else { ++b.bounces; k->bounceIdx.push_back((int)k->pts.size() - 1); }
        }
        b.v *= speedF; b.v0leg = b.v; b.distLeg = 0;
        if (b.v < C_.minSpeed) { restBall(R, b, t); return; }
        plan(b);
    }
    // v38: yaw the cue ball's outgoing direction by the spin angle after a cushion (game: spinQ * reflected). Skipped when the rotated direction would
    // point back into the wall (the game's own physics would just bounce it again; it also avoids a zero-length loop here).
    void applyEdgeSpin(B& b, int kind) {
        if (C_.edgeSpinDeg == 0 || cueIdx_ < 0 || &b != &B_[cueIdx_] || b.spinHits >= C_.edgeSpinBounces) return;
        const double a = C_.edgeSpinDeg * 3.14159265358979323846 / 180.0, cs = std::cos(a), sn = std::sin(a);
        V2 d{b.d.x * cs + b.d.z * sn, b.d.z * cs - b.d.x * sn};
        if (kind == 1 && d.x * b.d.x <= 0.01) return;          // x-wall: the x component must keep pointing away from the wall
        if (kind == 2 && d.z * b.d.z <= 0.01) return;
        if (kind == 5) {
            V2 n = b.ch.n; double nl = len(n); if (nl < 0.3) return; n = n * (1.0 / nl); if (dot(b.d, n) < 0) n = n * -1.0;   // b.d is already reflected: n now points away from the wall
            if (dot(d, n) <= 0.01) return;
        }
        ++b.spinHits; b.d = d;
    }
    void restBallAt(Result& R, B& b, double t) { b.sEvt = 0; restBall(R, b, t); }

    void pocketBall(Result& R, B& b, double t) {
        V2 P = T_.pockets[b.idx].p; double s = dot(P - b.p, b.d); V2 e = s > 0 ? b.p + b.d * s : b.p;   // stay ON the line (no snap to the centre)
        if (Track* k = tr(R, b)) { k->pts.push_back(e); k->end = END_POCKET; k->pocket = b.idx; k->t1 = t; }
        b.p = e; b.out = true; b.moving = false; b.pocket = b.idx; b.v = 0; b.tRest = t;
    }

    void simulate(Result& R) {
        for (auto& b : B_) if (b.moving) plan(b);
        double t = 0;
        const auto t0w = std::chrono::steady_clock::now();
        for (int guard = 0; guard < 400000 && t < C_.tMax; ++guard) {
            if (C_.budgetMs > 0 && (guard & 31) == 31 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0w).count() > C_.budgetMs) break;
            bool any = false; for (auto& b : B_) if (b.moving && !b.out) { any = true; break; }
            if (!any) break;
            double h = C_.dt; int evBall = -1;
            for (size_t i = 0; i < B_.size(); ++i) {                     // static events (cushion / pocket)
                B& b = B_[i]; if (!b.moving || b.out || b.sEvt > 1e17) continue;
                double te = timeForDist(b, b.sEvt);
                if (te < h) { h = te; evBall = (int)i; }
            }
            int pa = -1, pc = -1; double hp = h;
            for (size_t i = 0; i < B_.size(); ++i) {                     // pair contacts inside [0,h]
                if (B_[i].out) continue;
                for (size_t j = i + 1; j < B_.size(); ++j) {
                    if (B_[j].out || (!B_[i].moving && !B_[j].moving)) continue;
                    double tau = pairContact(B_[i], B_[j], h);
                    if (tau < 0) continue;
                    // tau is a fraction of the DISPLACEMENT over the step (the paths are straight, only the speed along them is non-linear): turn it back into time
                    const B& f = B_[i].moving && (!B_[j].moving || B_[i].v >= B_[j].v) ? B_[i] : B_[j];
                    double S = f.v / kOf(f) * (1.0 - std::exp(-kOf(f) * h)), th = tau >= 1.0 ? h : (S > 1e-15 ? timeForDist(f, tau * S) : 0.0);
                    if (th < hp || pa < 0) { if (th <= h + 1e-15) { hp = std::min(th, h); pa = (int)i; pc = (int)j; } }
                }
            }
            bool pairEv = pa >= 0 && hp <= h + 1e-15;
            double step = pairEv ? hp : h;
            for (auto& b : B_) if (b.moving && !b.out) advance(b, step);
            t += step;
            if (pairEv) {
                B& A = B_[pa]; B& Cc = B_[pc];
                // the one that is moving into the other is the "mover"; for two movers a is just first
                int ia = pa, ic = pc;
                if (!A.moving && Cc.moving) { ia = pc; ic = pa; }
                ballBall(R, ia, ic, t);
            } else if (evBall >= 0) {
                B& b = B_[evBall];
                if (b.kind == 4) pocketBall(R, b, t);
                else cushion(R, b, t);
            }
            // balls that slowed down below the rest threshold
            for (auto& b : B_) if (b.moving && !b.out && b.v < C_.minSpeed) restBall(R, b, t);
        }
        for (auto& b : B_) if (b.moving && !b.out) restBall(R, b, t);
    }
};


// v39: CueBall.OnHandleCueDirection / PhysicsBuilder.PredictCueDirAfterBallCollision, ported from the game binary (analysis/game_asm).
// Unity is left handed: Quaternion.Euler(0,t,0) * (x,y,z) = (x cos t + z sin t, y, z cos t - x sin t).
namespace gamecue {
struct V3 { double x, y, z; };
inline V3 yaw(double deg, V3 v) { const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a); return {v.x * c + v.z * s, v.y, v.z * c - v.x * s}; }
// IntBallAngle = (int)Math.Round( acos( -dot(up, Euler(0,-90)*n) / (|up||rot|) ) * Rad2Deg )   (Math.Round = banker's rounding)
inline int intBallAngle(V3 n, V3 up) {
    V3 r = yaw(-90.0, n); double lu = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z), lr = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (lu < 1e-9 || lr < 1e-9) return 90;
    double c = -(up.x * r.x + up.y * r.y + up.z * r.z) / (lu * lr); c = c > 1 ? 1 : (c < -1 ? -1 : c);
    return (int)std::nearbyint(std::acos(c) * 180.0 / 3.14159265358979323846);
}
// PredictCueRotationAngelAfterCollision (0x5DD7490): target of the SmoothDamp (the value this[0x254] converges to)
inline double classicTarget(int a) {
    if (a > 180) return 180; if (a < 88) return 95; if (a < 90) return 150; if (a == 90) return 180; if (a <= 92) return 210; return 265;
}
// PredictCueDirAfterBallCollision (0x5D420A8): Q = (euler(Qspin).y != 0) ? Qspin : Qclassic ; result = Q * n
inline V3 dirAfterBall(V3 n, double spinDeg, double classicDeg) {
    double w = std::fmod(spinDeg, 360.0); if (w < 0) w += 360.0;
    return yaw((w > 1e-4 && w < 360.0 - 1e-4) ? spinDeg : classicDeg, n);
}
}  // namespace gamecue

// v65 (phase 1): what the REAL PhysX numbers of the game mean for the model. Pure functions, no Unity calls.
namespace px {
// UnityEngine.PhysicMaterialCombine as stored by Unity: Average = 0, Multiply = 1, Minimum = 2, Maximum = 3.
// PhysX resolves a pair of materials with the combine mode of HIGHER priority: Average < Minimum < Multiply < Maximum.
inline int combineRank(int unityMode) { switch (unityMode) { case 0: return 0; case 2: return 1; case 1: return 2; case 3: return 3; default: return -1; } }
// value of one pair (bounciness or friction). Returns -1 when a mode / value is unknown.
inline double combine(int modeA, int modeB, double a, double b) {
    int ra = combineRank(modeA), rb = combineRank(modeB); if (ra < 0 || rb < 0 || a < 0 || b < 0) return -1.0;
    int m = ra >= rb ? modeA : modeB;
    switch (m) { case 0: return 0.5 * (a + b); case 1: return a * b; case 2: return std::min(a, b); case 3: return std::max(a, b); default: return -1.0; }
}
// A solid sphere that ROLLS without slipping under Rigidbody.drag (linear damping dl) and Rigidbody.angularDrag (angular damping da):
//   (m + I/r^2) dv/dt = -(m dl + (I/r^2) da) v , I = 2/5 m r^2   ->   rate = (dl + 0.4 da) / 1.4
// The angular damping works through the rolling friction, so the real stopping distance is v0/rate, not v0/dl.
inline double rollingDrag(double dl, double da) { return (dl + 0.4 * da) / 1.4; }
}  // namespace px
} // namespace pp
