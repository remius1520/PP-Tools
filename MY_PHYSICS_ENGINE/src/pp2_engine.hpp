// PP2 simulator engine (Level 11 first; the data/ folder is generated from the game's own files).
// Header-only, no dependencies, C++17.  See docs/CHANGES_v2.md for what is decoded vs assumed.
//
// Physics model (everything below comes from the game's files unless marked ASSUMED):
//   * ball = frictionless sphere, Y frozen, radius 0.5*0.055, mass 10, linearDamping 0.62          [globalgamemanagers/sharedassets0]
//   * 7 sub-steps of fixedDelta/7 (float32): substeps=7.0f from level5 ServerTracker (P2Q) -- but in the game this loop only runs on the
//     "special" path when the cue-ball SphereCast flag (+0x108) is true; otherwise Physics.Simulate(fixedDelta) once (NOT modelled, docs/FINDINGS_P2Q.md)
//     damping applied at the start of each sub-step                                                 [damping ORDER = ASSUMED, --drag-mode]
//   * contacts are solved at the EXACT time of impact (continuous), impulse along the contact normal  [frictionless => exact]
//   * restitution 0.92 (Edge/Pocket material, ball-ball), 0 for surfaces without material           [combine value 2 -> PxCombineMode eMIN: libunity table 0x209e34, see docs/FINDINGS_P2P.md]
//   * first contact of the cue ball is NOT PhysX: the game's server overrides both velocities with the
//     client's prediction (SetTargetBallAfterCollision / SetCueDirectionAfterBallCollide)             [decoded from libil2cpp, see ref/]
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <tuple>
#include <cstdint>
#include "pp2_clientpredict.hpp"
#include "pp2_solvers.hpp"
#include "pp2_servertracker.hpp"

namespace pp2 {

struct V2 { double x = 0, z = 0; };
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.z + b.z}; }
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.z - b.z}; }
inline V2 operator*(V2 a, double s) { return {a.x * s, a.z * s}; }
inline double dot(V2 a, V2 b) { return a.x * b.x + a.z * b.z; }
inline double len(V2 a) { return std::sqrt(dot(a, a)); }
inline V2 unit(V2 a) { double n = len(a); return n < 1e-9 ? V2{0, 0} : V2{a.x / n, a.z / n}; }

struct V3 { double x, y, z; };
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 mul(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross3(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

struct Cfg {
    double R = 0.0275;            // 0.5 * 0.055   (sharedassets0: SphereCollider 0.5, scale 0.055)
    double Y = -0.0296;           // frozen centre height (Level 11 prefab)
    double fixedDelta = 1411199.0 / 141120000.0;  // TimeManager RationalTime: count*den/num (data.unity3d + libunity 0x6a118c) = 0.00999999291 s
    float substepsF = 7.0f;       // ServerTracker.substeps (+0x140), level5 PathID 84 = 7.0f (P2Q). Applies ONLY to the game's flag-true special path (see header)
    int substeps = 7;             // = st::substepCount(substepsF): number of Simulate calls (loop 0x5DB8798: do{Simulate}while(substeps>i))
    double dt = (double)((float)(1411199.0 / 141120000.0) / 7.0f);  // float32 fixedDeltaTime / substeps (fdiv s8,s8,s15 at 0x5DB876C)
    void setSubsteps(float s) { substepsF = s; substeps = st::substepCount(s); dt = substeps ? (double)st::substepDt((float)fixedDelta, s) : fixedDelta; }
    double drag = 0.62;           // Rigidbody.linearDamping  [GAME_FILE_FACTS: confirmed from sharedassets0]
    int dragMode = 0;             // 0: v*=max(0,1-d*dt)  1: v/=(1+d*dt)   [BINARY: PreIntegrateTask::runInternal@0x146a464 — gravity → v*=max(0,1-d·dt) → clamp, float32, proven by disassembly]
    double angularDrag = 1.0;     // Rigidbody.angularDamping  [GAME_FILE_FACTS: confirmed from sharedassets0]
    // Sleep model: dragMode proven [BINARY]; sleepKE=0 stays OFF [BINARY: sleep-check@0x13cc6e8, ctor@0x13df9a0 — no 0.001 KE threshold in binary; KE threshold at 0x13cc8b8 uses wakeCounter×0.4f from BodyCore+0x9C].
    // wakeCounter=0.4 is PhysX default [PHYSX_4_1_REFERENCE]; bodySleepThreshold=0.002f is game-level Ball.sleepThreshold [GAME_FILE: dump.cs + runtime dumps +0xC4].
    double sleepThreshold = 0.001;// Physics.sleepThreshold  [GAME_FILE_FACTS: data.unity3d PhysicsManager; written to BodyCore+0x98 via setSleepThreshold@0x13e0bc8, NOT used as KE threshold in binary]
    double bodySleepThreshold = 0.002; // Ball.sleepThreshold (+0xC4)  [GAME_FILE: dump.cs + runtime dumps tables 5/7/9/11 = 0.002f]
    double wakeCounter = 0.4;     // seconds below threshold before sleep  [PHYSX_4_1_REFERENCE: PhysX 4.1 default wake counter]
    // sleepKE = mass-normalised KE threshold (per unit mass): 0.5*(vx^2+vz^2+aw^2) < sleepKE -> sleep candidate.
    // Default = 0 (OFF): sleep/wake disabled until binary proves a KE threshold. Binary sleep-check@0x13cc8b8 compares 0.5×KE vs wakeCounter×0.4f (BodyCore+0x9C), NOT sleepKE.
    double sleepKE = 0;
    // CCD configuration: ccdMode=0 (continuous TOI). CCD flag consumption NOT located in libunity.so disassembly [NOT FOUND from binary] → PHYSX_4_1_REFERENCE, stays OFF.
    int ccdMode = 0;              // 0: continuous/analytical TOI (current engine)  1: discrete sub-step only
    double maxDepenetrationVelocity = 0.0; // 0 = OFF. [REVIEW A: the value 10 (Physics.defaultMaxDepenetrationVelocity) is from game files, BUT PhysX uses it only to limit the speed of overlap resolution, NOT to cap per-sub-step displacement. The cap below is an invented behaviour (it changed 55 of 120 break/high-force shots by up to 1.9 m at default 10). Kept only as an experimental switch; must stay 0 unless a game-binary decode proves otherwise.]
    double eBall = 0.92;          // ball-ball restitution
    double eEdge = 0.92;          // Edges_Physics
    double ePocket = 0.92;        // Pocket_Physics
    double eDefault = 0.0;        // surfaces without material (Minimum with 0)
    double stop = 0.002;          // ServerTracker.stopSimulateThreshold (+0x144, level5 = 0.002f): ONLY the special path's snap (flag108=false, |target-pos| < stop -> position=target). P2Z3: NOT the end-of-shot test (0x5DB79D4 IsCueBallMoving reads ANGULAR velocity and has no callers)
    double ballStop = 0.002;      // Ball.sleepThreshold (+0xC4; 0.002f in every runtime dump, tables 5/7/9/11): Ball.IsMoving (0x5DD6618) = |linearVelocity| >= this. Shot ends when no ball IsMoving (GameManagerService.BallInAction 0x5DC3B04 <- OnPingBallVelocity 0x5DC3A7C)
    double tmax = 60;
    double sleepTime = 0.4;       // alias for wakeCounter (kept for CLI/API compatibility)
    double velClamp = 20.0;       // game clamp
    bool firstContact = true;     // game's server override of the first cue contact
    bool solveOverlap = true;     // PhysicsBuilder.SolveBalls after the simulation ends (position-only overlap fix), decoded: src/pp2_solvers.hpp
    int solvePasses = 8;          // ServerTracker.solvecount (+0x148), level5 PathID 84 = 8 (P2Q)
    double contactOffset = 0.001; // Physics.defaultContactOffset (PhysicsManager)
    // P2R: ServerTracker.OnServerSimulate special path (0x5DB856C) -- cue ball before its first non-"Table" contact:
    //   every FixedUpdate: cue.linearVelocity = (baseline + normalize(vec)*|vec|) - cue.position   (vec = ShotData forceDirection, baseline = cue position at the shot request)
    //   flag +0x108 (SphereCast along vec, radius r+2*defaultContactOffset, maxDistance |vec|): true -> `substeps` sub-steps of fixedDelta/substeps; false -> ONE step of fixedDelta
    //   (and if |target-pos| < stopSimulateThreshold: position=target, v=0, special path off).  After the cue's first contact (CueBall.collided +0x26C): ONE Simulate(fixedDelta) per tick.
    //   special=false -> old behaviour (cue gets dir*F once, `substeps` sub-steps every tick).
    bool special = true;
    // P2R: layerMask of the flag-+0x108 SphereCast = SpinController.spinLayer (+0xF8) = 0xC00 (level5, data/spin_controller_level5.json) = layers 10 (BallBase, balls) | 11 (Edge walls).
    // Table/pocket-mesh colliders (layer 0) are NOT in the mask.  The pocket trigger boxes' layer is unconfirmed (assumed outside the mask): castPockets=true adds them.
    unsigned castMask = 0xC00; bool castPockets = false;
    // P2S: the CheckBall pocket triggers are CONVEX MeshColliders (isTrigger, layer 3, tag PocketInside) -- not boxes (sharedassets0.assets, FINDINGS_P2S).
    // true: the ball centre must enter the xz convex hull of that mesh (same centre-in-shape rule as the old AABB rule); false: old AABB (54-78% larger area).
    bool pocketHull = true;
    // P2T: the game scores a pocket from Unity's OnTriggerEnter (Ball.OnTriggerEnter 0x5DD1F40 -> vtable slot 34 OnTriggerOccur 0x5DD38C8 -> Icollision.OnCollisionOccuer): PhysX trigger = ball SPHERE overlaps the convex mesh.
    // true: first contact of the ball sphere (radius R) with the hull (vertical offset to the hull's y-range included, side walls treated as vertical); false: centre-in-hull (P2S).
    bool pocketSphere = true;
    // P2X: true (default, when the table has data/<key>_pocket_hull3d.txt): ball sphere vs the TRUE 3D convex hull of the trigger mesh (overlap <=> distance(centre, hull) <= R, general PhysX knowledge:
    // GJK overlap of sphere and convex, no contactOffset for triggers); false: the P2T xz-prism rule.
    bool pocketHull3d = true;
};

struct Tri { V3 a, b, c; char mat; int layer; V3 lo, hi; };
struct Pocket { std::string name; V3 lo, hi; std::vector<V2> hull; std::vector<Tri> h3; };   // h3 (P2X) = 3D convex-hull triangles of the trigger mesh (outward winding), world; empty -> prism/AABB rules   // hull = xz convex hull of the trigger MeshCollider (P2S); empty -> AABB lo/hi only

inline V3 closestPoint(V3 p, const Tri& t) {   // Ericson, Real-Time Collision Detection
    V3 ab = sub(t.b, t.a), ac = sub(t.c, t.a), ap = sub(p, t.a);
    double d1 = dot3(ab, ap), d2 = dot3(ac, ap); if (d1 <= 0 && d2 <= 0) return t.a;
    V3 bp = sub(p, t.b); double d3 = dot3(ab, bp), d4 = dot3(ac, bp); if (d3 >= 0 && d4 <= d3) return t.b;
    double vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(t.a, mul(ab, d1 / (d1 - d3)));
    V3 cp = sub(p, t.c); double d5 = dot3(ab, cp), d6 = dot3(ac, cp); if (d6 >= 0 && d5 <= d6) return t.c;
    double vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(t.a, mul(ac, d2 / (d2 - d6)));
    double va = d3 * d6 - d5 * d4; if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return add(t.b, mul(sub(t.c, t.b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    double den = 1.0 / (va + vb + vc); return add(t.a, add(mul(ab, vb * den), mul(ac, vc * den)));
}

struct WallHit { bool hit = false; double t = 0; V3 n{0, 0, 0}; int tri = -1; };

struct World {
    std::vector<Tri> T; std::vector<Pocket> P;
    std::vector<solver::Aabb> rails;   // AABB of each cushion collider (connected set of 'E' triangles) = Collider.bounds used by ClosestPointOnBounds
    // uniform grid over xz
    double gx0 = 0, gz0 = 0, cell = 0.05; int nx = 0, nz = 0;
    std::vector<std::vector<int>> grid; mutable std::vector<int> stamp; mutable int stampId = 0;

    bool load(const std::string& dir, const std::string& key = "level11") {
        T.clear(); P.clear();
        FILE* f = fopen((dir + "/" + key + "_solids.txt").c_str(), "r"); if (!f) return false; char line[512];
        while (fgets(line, 512, f)) {
            if (line[0] == '#') continue; Tri t; double v[9]; char m; int l;
            if (sscanf(line, "%c %d %lf %lf %lf %lf %lf %lf %lf %lf %lf", &m, &l, v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, v + 8) != 11) continue;
            t.a = {v[0], v[1], v[2]}; t.b = {v[3], v[4], v[5]}; t.c = {v[6], v[7], v[8]}; t.mat = m; t.layer = l;
            t.lo = {std::min({v[0], v[3], v[6]}), std::min({v[1], v[4], v[7]}), std::min({v[2], v[5], v[8]})};
            t.hi = {std::max({v[0], v[3], v[6]}), std::max({v[1], v[4], v[7]}), std::max({v[2], v[5], v[8]})};
            T.push_back(t);
        }
        fclose(f);
        f = fopen((dir + "/" + key + "_pockets.txt").c_str(), "r"); if (!f) return false;
        while (fgets(line, 512, f)) {
            if (line[0] == '#') continue; char n[64]; double v[6];
            if (sscanf(line, "%63s %lf %lf %lf %lf %lf %lf", n, v, v + 1, v + 2, v + 3, v + 4, v + 5) != 7) continue;
            P.push_back({n, {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {}, {}});
        }
        fclose(f);
        f = fopen((dir + "/" + key + "_pocket_hulls.txt").c_str(), "r");   // optional (P2S)
        if (f) {
            static char big[4096];
            while (fgets(big, 4096, f)) {
                if (big[0] == '#') continue; char n[64]; int off = 0, k = 0;
                if (sscanf(big, "%63s %d%n", n, &k, &off) != 2 || k < 3) continue;
                std::vector<V2> h; const char* c = big + off; bool ok = true;
                for (int i = 0; i < k && ok; ++i) { double x, z; int m = 0; if (sscanf(c, "%lf %lf%n", &x, &z, &m) != 2) ok = false; else { h.push_back({x, z}); c += m; } }
                if (!ok) continue; for (auto& p : P) if (p.name == n) p.hull = h;
            }
            fclose(f);
        }
        f = fopen((dir + "/" + key + "_pocket_hull3d.txt").c_str(), "r");   // optional (P2X): 3D hull triangles
        if (f) {
            std::string buf; char chunk[8192];
            while (fgets(chunk, sizeof chunk, f)) { buf += chunk; if (!buf.empty() && buf.back() == '\n') { loadHull3dLine(buf); buf.clear(); } }
            if (!buf.empty()) loadHull3dLine(buf);
            fclose(f);
        }
        buildGrid(); buildRails(); return !T.empty();
    }
    void loadHull3dLine(const std::string& line) {
        if (line.empty() || line[0] == '#') return; char n[64]; int off = 0, k = 0;
        if (sscanf(line.c_str(), "%63s %d%n", n, &k, &off) != 2 || k < 4) return;
        std::vector<Tri> h; const char* c = line.c_str() + off;
        for (int i = 0; i < k; ++i) {
            double v[9]; int m = 0; if (sscanf(c, "%lf %lf %lf %lf %lf %lf %lf %lf %lf%n", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, v + 8, &m) != 9) return; c += m;
            Tri t; t.a = {v[0], v[1], v[2]}; t.b = {v[3], v[4], v[5]}; t.c = {v[6], v[7], v[8]}; t.mat = 'H'; t.layer = 3;
            t.lo = {std::min({v[0], v[3], v[6]}), std::min({v[1], v[4], v[7]}), std::min({v[2], v[5], v[8]})}; t.hi = {std::max({v[0], v[3], v[6]}), std::max({v[1], v[4], v[7]}), std::max({v[2], v[5], v[8]})};
            h.push_back(t);
        }
        for (auto& p : P) if (p.name == n) p.h3 = h;
    }
    // v4: same data as the text files, packed (tools/pack_data.py): 'PP2D' u32 nTri u32 nPocket | tri: u8 mat, u8 layer, 9 x f64 | pocket: char[16] name, 6 x f64 (doubles = identical to the text files, so results equal the CLI bit for bit)
    bool loadBinary(const unsigned char* d, size_t n) {
        T.clear(); P.clear(); if (n < 12 || memcmp(d, "PP2D", 4)) return false;
        uint32_t nt, np; memcpy(&nt, d + 4, 4); memcpy(&np, d + 8, 4); const size_t TS = 2 + 9 * 8, PS = 16 + 6 * 8;
        if (n < 12 + nt * TS + np * PS) return false; const unsigned char* q = d + 12;
        for (uint32_t i = 0; i < nt; ++i, q += TS) {
            double v[9]; memcpy(v, q + 2, 72); Tri t; t.mat = (char)q[0]; t.layer = q[1];
            t.a = {v[0], v[1], v[2]}; t.b = {v[3], v[4], v[5]}; t.c = {v[6], v[7], v[8]};
            t.lo = {std::min({v[0], v[3], v[6]}), std::min({v[1], v[4], v[7]}), std::min({v[2], v[5], v[8]})};
            t.hi = {std::max({v[0], v[3], v[6]}), std::max({v[1], v[4], v[7]}), std::max({v[2], v[5], v[8]})};
            T.push_back(t);
        }
        for (uint32_t i = 0; i < np; ++i, q += PS) { char nm[17]; memcpy(nm, q, 16); nm[16] = 0; double v[6]; memcpy(v, q + 16, 48); P.push_back({nm, {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {}, {}}); }
        // optional trailing section (P2S): 'HULL' u32 nh | nh x { char[16] name, u32 n, n x 2 f64 }
        if (n >= (size_t)(q - d) + 8 && !memcmp(q, "HULL", 4)) {
            uint32_t nh; memcpy(&nh, q + 4, 4); q += 8;
            for (uint32_t i = 0; i < nh && (size_t)(d + n - q) >= 20; ++i) {
                char nm[17]; memcpy(nm, q, 16); nm[16] = 0; uint32_t k; memcpy(&k, q + 16, 4); q += 20;
                if ((size_t)(d + n - q) < (size_t)k * 16) break;
                std::vector<V2> h; for (uint32_t j = 0; j < k; ++j, q += 16) { double xz[2]; memcpy(xz, q, 16); h.push_back({xz[0], xz[1]}); }
                for (auto& p : P) if (p.name == nm) p.hull = h;
            }
        }
        // optional trailing section (P2X): 'HUL3' u32 n | n x { char[16] name, u32 ntri, ntri x 9 f64 }   (follows the HULL section)
        if (n >= (size_t)(q - d) + 8 && !memcmp(q, "HUL3", 4)) {
            uint32_t nh; memcpy(&nh, q + 4, 4); q += 8;
            for (uint32_t i = 0; i < nh && (size_t)(d + n - q) >= 20; ++i) {
                char nm[17]; memcpy(nm, q, 16); nm[16] = 0; uint32_t k; memcpy(&k, q + 16, 4); q += 20;
                if ((size_t)(d + n - q) < (size_t)k * 72) break;
                std::vector<Tri> h;
                for (uint32_t j = 0; j < k; ++j, q += 72) {
                    double v[9]; memcpy(v, q, 72); Tri t; t.a = {v[0], v[1], v[2]}; t.b = {v[3], v[4], v[5]}; t.c = {v[6], v[7], v[8]}; t.mat = 'H'; t.layer = 3;
                    t.lo = {std::min({v[0], v[3], v[6]}), std::min({v[1], v[4], v[7]}), std::min({v[2], v[5], v[8]})}; t.hi = {std::max({v[0], v[3], v[6]}), std::max({v[1], v[4], v[7]}), std::max({v[2], v[5], v[8]})};
                    h.push_back(t);
                }
                for (auto& p : P) if (p.name == nm) p.h3 = h;
            }
        }
        buildGrid(); buildRails(); return !T.empty();
    }
    // Cushion colliders = connected components of 'E' triangles (shared vertices).  Level 11: 6 rails.
    void buildRails() {
        rails.clear(); std::vector<int> id; for (size_t i = 0; i < T.size(); ++i) if (T[i].mat == 'E') id.push_back((int)i);
        std::vector<int> par(id.size()); for (size_t i = 0; i < par.size(); ++i) par[i] = (int)i;
        auto fnd = [&](int a) { while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; } return a; };
        std::map<std::tuple<long, long, long>, int> vm;
        auto key = [](V3 v) { return std::make_tuple(std::lround(v.x * 1e5), std::lround(v.y * 1e5), std::lround(v.z * 1e5)); };
        for (size_t k = 0; k < id.size(); ++k) for (V3 v : {T[id[k]].a, T[id[k]].b, T[id[k]].c}) {
            auto it = vm.find(key(v)); if (it == vm.end()) vm[key(v)] = (int)k; else par[fnd((int)k)] = fnd(it->second); }
        std::map<int, solver::Aabb> g;
        for (size_t k = 0; k < id.size(); ++k) { int r = fnd((int)k); const Tri& t = T[id[k]];
            auto ins = g.emplace(r, solver::Aabb{{t.lo.x, t.lo.y, t.lo.z}, {t.hi.x, t.hi.y, t.hi.z}});
            if (!ins.second) { solver::Aabb& b = ins.first->second; b.lo = {std::min(b.lo.x, t.lo.x), std::min(b.lo.y, t.lo.y), std::min(b.lo.z, t.lo.z)}; b.hi = {std::max(b.hi.x, t.hi.x), std::max(b.hi.y, t.hi.y), std::max(b.hi.z, t.hi.z)}; } }
        for (auto& kv : g) rails.push_back(kv.second);
    }
    void buildGrid() {
        double x0 = 1e9, x1 = -1e9, z0 = 1e9, z1 = -1e9;
        for (auto& t : T) { x0 = std::min(x0, t.lo.x); x1 = std::max(x1, t.hi.x); z0 = std::min(z0, t.lo.z); z1 = std::max(z1, t.hi.z); }
        gx0 = x0 - 0.01; gz0 = z0 - 0.01; nx = (int)std::ceil((x1 - gx0) / cell) + 1; nz = (int)std::ceil((z1 - gz0) / cell) + 1;
        grid.assign((size_t)nx * nz, {}); stamp.assign(T.size(), 0);
        for (size_t i = 0; i < T.size(); ++i) {
            int ix0 = cl((int)std::floor((T[i].lo.x - gx0) / cell), nx), ix1 = cl((int)std::floor((T[i].hi.x - gx0) / cell), nx);
            int iz0 = cl((int)std::floor((T[i].lo.z - gz0) / cell), nz), iz1 = cl((int)std::floor((T[i].hi.z - gz0) / cell), nz);
            for (int a = ix0; a <= ix1; ++a) for (int b = iz0; b <= iz1; ++b) grid[(size_t)a * nz + b].push_back((int)i);
        }
    }
    static int cl(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }

    // Exact time of impact of a sphere (centre c0 at height Y, radius R) moving by disp (xz) against the triangle set.
    // dist(c(t), triangle) is convex in t, so Newton steps converge to the first root: exact (<1e-10 m), not a discretisation.
    // Numerical safeguards: tt clamped to [0,1], df near-zero guard, max 60 iterations, epsilon on f comparison.
    // Only surfaces the ball is CLOSING on count.  maskE: only mat 'E' (used by nothing now, kept for the cast).
    WallHit sweep(V2 c0, V2 disp, double R, double Y, unsigned layerMask = ~0u) const {
        WallHit best; double L = len(disp); if (L < 1e-15) return best;
        double m = R + 1e-6;
        double lx = std::min(c0.x, c0.x + disp.x) - m, hx = std::max(c0.x, c0.x + disp.x) + m;
        double lz = std::min(c0.z, c0.z + disp.z) - m, hz = std::max(c0.z, c0.z + disp.z) + m;
        int ix0 = cl((int)std::floor((lx - gx0) / cell), nx), ix1 = cl((int)std::floor((hx - gx0) / cell), nx);
        int iz0 = cl((int)std::floor((lz - gz0) / cell), nz), iz1 = cl((int)std::floor((hz - gz0) / cell), nz);
        ++stampId; V3 dv{disp.x, 0, disp.z};
        for (int a = ix0; a <= ix1; ++a) for (int b = iz0; b <= iz1; ++b) for (int i : grid[(size_t)a * nz + b]) {
            if (stamp[i] == stampId) continue; stamp[i] = stampId; const Tri& t = T[i];
            if (Y + R < t.lo.y || Y - R > t.hi.y) continue;
            if (!((layerMask >> (t.layer & 31)) & 1u)) continue;
            if (t.hi.x < lx || t.lo.x > hx || t.hi.z < lz || t.lo.z > hz) continue;
            double tt = 0;
            for (int it = 0; it < 60; ++it) {
                V3 c{c0.x + disp.x * tt, Y, c0.z + disp.z * tt}; V3 p = closestPoint(c, t); V3 d = sub(c, p); double dist = std::sqrt(dot3(d, d));
                double f = dist - R;
                if (dist < 1e-12) break;                       // centre on the surface: degenerate, ignore
                V3 n = mul(d, 1.0 / dist); double df = dot3(n, dv);     // d(dist)/dt
                if (f <= 1e-10) {                               // touching / inside
                    if (df < -1e-12 && (n.x * n.x + n.z * n.z) > 1e-9 && (!best.hit || tt < best.t)) { best.hit = true; best.t = std::clamp(tt, 0.0, 1.0); best.n = n; best.tri = i; }
                    break;
                }
                if (df >= -1e-14) break;                        // not closing => convex => never reaches
                double step = f / (-df);
                if (!std::isfinite(step) || step < 0) break;    // numerical safeguard: non-finite or negative step
                tt += step; if (tt > 1.0) { tt = 1.0; break; }  // clamp to segment end
            }
        }
        return best;
    }
    // Discrete CCD (mode 1): check if ball at position cp penetrates any wall triangle.
    // Returns true and fills wh with hit info (normal from closest point) if penetrating.
    bool discreteWallHit(V2 cp, double R, double Y, WallHit& wh) const {
        for (size_t i = 0; i < T.size(); ++i) {
            const Tri& t = T[i];
            if (Y + R < t.lo.y || Y - R > t.hi.y) continue;
            if (!((~0u >> (t.layer & 31)) & 1u)) continue;
            V3 c{cp.x, Y, cp.z}; V3 p = closestPoint(c, t); V3 d = sub(c, p); double dist = std::sqrt(dot3(d, d));
            if (dist < R) {
                V3 n = dist > 1e-12 ? mul(d, 1.0 / dist) : V3{0, 0, 0};
                wh.hit = true; wh.t = 0; wh.n = n; wh.tri = (int)i;
                return true;
            }
        }
        return false;
    }
    // P2X: distance from point q to the convex polyhedron given by outward-wound triangles (0 if inside).
    static double hullDist3(const std::vector<Tri>& h, V3 q) {
        bool inside = true; double bd = 1e300;
        for (const Tri& t : h) {
            V3 n = cross3(sub(t.b, t.a), sub(t.c, t.a)); double L = std::sqrt(dot3(n, n)); if (L > 0 && dot3(n, sub(q, t.a)) / L > 1e-12) inside = false;
            V3 c = closestPoint(q, t); V3 d = sub(q, c); bd = std::min(bd, dot3(d, d));
        }
        return inside ? 0.0 : std::sqrt(bd);
    }
    // does the segment c0 -> c0+disp (centre) enter a pocket trigger (grown by R in y only, like the old AABB rule)? returns t in [0,1], -1 none
    // P2S: when `useHull` and the pocket has its xz convex hull, the segment is clipped against that convex polygon (Cyrus-Beck) instead of the AABB.
    double pocketEntry(V2 c0, V2 disp, double R, double Y, int& which, bool useHull = true, bool sphere = false, bool hull3d = true) const {
        double best = -1; which = -1;
        for (size_t k = 0; k < P.size(); ++k) {
            const Pocket& p = P[k]; if (Y < p.lo.y - R || Y > p.hi.y + R) continue;
            if (sphere && hull3d && p.h3.size() >= 4) {   // P2X: sphere vs the true 3D convex hull: first t with dist(centre(t), hull) <= R (distance to a convex set is convex along a line)
                {   // fast reject: segment (xz) vs AABB grown by R
                    double o2[2] = {c0.x, c0.z}, d2[2] = {disp.x, disp.z}, l2[2] = {p.lo.x - R, p.lo.z - R}, h2[2] = {p.hi.x + R, p.hi.z + R}; double t0 = 0, t1 = 1; bool ok = true;
                    for (int ax = 0; ax < 2 && ok; ++ax) {
                        if (std::fabs(d2[ax]) < 1e-15) { if (o2[ax] < l2[ax] || o2[ax] > h2[ax]) ok = false; }
                        else { double ta = (l2[ax] - o2[ax]) / d2[ax], tb = (h2[ax] - o2[ax]) / d2[ax]; if (ta > tb) std::swap(ta, tb); t0 = std::max(t0, ta); t1 = std::min(t1, tb); if (t0 > t1) ok = false; }
                    }
                    if (!ok) continue;
                }
                auto dist = [&](double t) { return hullDist3(p.h3, {c0.x + disp.x * t, Y, c0.z + disp.z * t}); };
                if (dist(0) <= R) { if (best < 0 || 0 < best) { best = 0; which = (int)k; } continue; }
                double lo = 0, hi = 1; for (int it = 0; it < 60; ++it) { double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3; if (dist(m1) < dist(m2)) hi = m2; else lo = m1; }
                double tm = 0.5 * (lo + hi); if (dist(tm) > R) continue;
                double a = 0, b = tm; for (int it = 0; it < 50; ++it) { double m = 0.5 * (a + b); if (dist(m) <= R) b = m; else a = m; }
                if (best < 0 || b < best) { best = b; which = (int)k; }
                continue;
            }
            if (sphere && useHull && p.hull.size() >= 3) {   // P2T: sphere-vs-convex-prism: first t with dist_xz(centre(t), hull) <= sqrt(R^2 - dy^2)
                double dy = std::max(0.0, std::max(p.lo.y - Y, Y - p.hi.y)); double rr = std::sqrt(std::max(0.0, R * R - dy * dy));
                {   // fast reject: segment vs AABB grown by rr (the hull lies inside its AABB, so no hit here => no hit at all)
                    double o2[2] = {c0.x, c0.z}, d2[2] = {disp.x, disp.z}, l2[2] = {p.lo.x - rr, p.lo.z - rr}, h2[2] = {p.hi.x + rr, p.hi.z + rr}; double t0 = 0, t1 = 1; bool ok = true;
                    for (int ax = 0; ax < 2 && ok; ++ax) {
                        if (std::fabs(d2[ax]) < 1e-15) { if (o2[ax] < l2[ax] || o2[ax] > h2[ax]) ok = false; }
                        else { double ta = (l2[ax] - o2[ax]) / d2[ax], tb = (h2[ax] - o2[ax]) / d2[ax]; if (ta > tb) std::swap(ta, tb); t0 = std::max(t0, ta); t1 = std::min(t1, tb); if (t0 > t1) ok = false; }
                    }
                    if (!ok) continue;
                }
                double ar0 = 0; { const std::vector<V2>& h = p.hull; size_t n = h.size(); for (size_t i = 0; i < n; ++i) ar0 += h[i].x * h[(i + 1) % n].z - h[(i + 1) % n].x * h[i].z; }
                const double sg0 = ar0 >= 0 ? 1.0 : -1.0;
                auto dist = [&](double t) {
                    double qx = c0.x + disp.x * t, qz = c0.z + disp.z * t; const std::vector<V2>& h = p.hull; size_t n = h.size();
                    const double sg = sg0; bool inside = true; double bd = 1e30;
                    for (size_t i = 0; i < n; ++i) {
                        const V2& a = h[i]; const V2& b = h[(i + 1) % n]; double ex = b.x - a.x, ez = b.z - a.z, L2 = ex * ex + ez * ez;
                        if ((-ez * sg) * (qx - a.x) + (ex * sg) * (qz - a.z) < 0) inside = false;
                        double u = L2 > 0 ? ((qx - a.x) * ex + (qz - a.z) * ez) / L2 : 0; u = std::min(1.0, std::max(0.0, u));
                        double cx = a.x + ex * u - qx, cz = a.z + ez * u - qz; bd = std::min(bd, cx * cx + cz * cz);
                    }
                    return inside ? 0.0 : std::sqrt(bd);
                };
                if (dist(0) <= rr) { if (best < 0 || 0 < best) { best = 0; which = (int)k; } continue; }
                double lo = 0, hi = 1; for (int it = 0; it < 60; ++it) { double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3; if (dist(m1) < dist(m2)) hi = m2; else lo = m1; }   // convex in t
                double tm = 0.5 * (lo + hi); if (dist(tm) > rr) continue;
                double a = 0, b = tm; for (int it = 0; it < 50; ++it) { double m = 0.5 * (a + b); if (dist(m) <= rr) b = m; else a = m; }
                if (best < 0 || b < best) { best = b; which = (int)k; }
                continue;
            }
            double t0 = 0, t1 = 1; bool ok = true;
            double o[2] = {c0.x, c0.z}, d[2] = {disp.x, disp.z}, lo[2] = {p.lo.x, p.lo.z}, hi[2] = {p.hi.x, p.hi.z};
            for (int a = 0; a < 2 && ok; ++a) {
                if (std::fabs(d[a]) < 1e-15) { if (o[a] < lo[a] || o[a] > hi[a]) ok = false; }
                else { double ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a]; if (ta > tb) std::swap(ta, tb); t0 = std::max(t0, ta); t1 = std::min(t1, tb); if (t0 > t1) ok = false; }
            }
            if (ok && useHull && p.hull.size() >= 3) {      // the hull lies inside its AABB: refine t0/t1 with the polygon's half-planes
                const std::vector<V2>& h = p.hull; size_t n = h.size(); double ar = 0;
                for (size_t i = 0; i < n; ++i) { const V2& a = h[i]; const V2& b = h[(i + 1) % n]; ar += a.x * b.z - b.x * a.z; }
                double sg = ar >= 0 ? 1.0 : -1.0;               // orientation-independent inward normals
                for (size_t i = 0; i < n && ok; ++i) {
                    const V2& a = h[i]; const V2& b = h[(i + 1) % n]; double ex = b.x - a.x, ez = b.z - a.z;
                    double nx = -ez * sg, nz = ex * sg;          // inward normal (unnormalised): inside <=> n.(q-a) >= 0
                    double s0 = nx * (c0.x - a.x) + nz * (c0.z - a.z), sd = nx * disp.x + nz * disp.z;
                    if (std::fabs(sd) < 1e-18) { if (s0 < 0) ok = false; }
                    else { double te = -s0 / sd; if (sd > 0) t0 = std::max(t0, te); else t1 = std::min(t1, te); if (t0 > t1) ok = false; }
                }
            }
            if (ok && (best < 0 || t0 < best)) { best = t0; which = (int)k; }
        }
        return best;
    }
};

struct Ball { int id; double x, z, vx, vz; bool in = true, pocketed = false; double tPocket = -1, sleepT = 0; double ax = 0, az = 0, aw = 0; double wakeCounter = 0; bool sleeping = false; };

struct Shot {
    int cueId = 0; double dx = 0, dz = 0;   // shot direction (xz, need not be unit)
    double F = 0;                           // |forceDirection| (initial cue speed). Flat cue assumed: F3 = horizontalForce
    double Fh = -1;                         // horizontalForce; <0 -> F
    double spin = -1;                       // SpinController.SpinFactor; <0 -> centred tip (= finalVelocity)
    bool spinApplied = false;
    double spinAngleDeg = 0;                // SpinController.SpinAngelDeg (0 = no spin: Ball uses the theta table, Edge = plain reflection)
    // P2M: CueBall.correctedRotAngel (+0x254) is Mathf.SmoothDamp'ed once per frame toward the theta-table target (smoothTime 0.03, vel +0x258, start vel assumed 0).
    // thetaFrames == 0 (default) or thetaStartDeg < 0: converged (old behaviour). Otherwise: start at thetaStartDeg and run thetaFrames steps of thetaDt (= Time.deltaTime, s).
    double thetaStartDeg = -1, thetaDt = 0.0226667; int thetaFrames = 0;
};

namespace game {  // decoded from libil2cpp.so (ref/p2e_core_sim/pp2_game.hpp, pp2_shot.hpp)
constexpr double kZeroSpin = 0.02, kMinCueSin = 0.04, kRad2Deg = 57.29578, kDeg2Rad = 0.017453292;
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double cueTheta(int a) { if (a > 180) return 180; if (a < 88) return 95; if (a < 90) return 150; if (a == 90) return 180; if (a <= 92) return 210; return 265; }
// Unity Mathf.SmoothDamp (6-arg, 0x6C16034), float arithmetic like the game; maxSpeed = +inf.
inline float smoothDampF(float cur, float tgt, float& vel, float smoothTime, float dt) {
    smoothTime = std::max(0.0001f, smoothTime); float om = 2.0f / smoothTime, x = om * dt;
    float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x), ch = cur - tgt, orig = tgt, t = (vel + om * ch) * dt;
    vel = (vel - om * t) * e; float o = tgt + (ch + t) * e;
    if ((orig - cur > 0.0f) == (o > orig)) { o = orig; vel = (o - orig) / dt; }
    return o;
}
inline double thetaAfterFrames(double start, double target, int frames, double dt) {
    float c = (float)start, v = 0.0f; for (int i = 0; i < frames; ++i) c = smoothDampF(c, (float)target, v, 0.03f, (float)dt); return c;
}
inline V2 rotY(V2 v, double deg) { double r = deg * kDeg2Rad, c = std::cos(r), s = std::sin(r); return {v.x * c + v.z * s, -v.x * s + v.z * c}; }
}

struct Prediction {
    bool valid = false, isBall = false; int targetId = -1;
    double hitDistance = 0, ballAngleDeg = 0, theta = 0, finalVelocity = 0, targetSpeed = 0; int angleInt = 0;
    V2 normal, cueDir, targetDir, cueV, targetV; char hitMat = 0;
};

struct PathPt { double t; std::vector<V2> p; };
// V3_2 merge: event log for track building.  kind 1 = cushion/solid (mat = triangle material), 2 = ball-ball, 3 = pocket (pocket = index), 4 = ball came to rest.
// a/b = indices into the ball vector, pa/pb = positions, sa/sb = speeds after the event (kind 1/2).  Only filled when run(..., wantEv=true).
struct TEv { int kind; double t; int a, b; V2 pa, pb; double sa, sb; int pocket; char mat; };
struct Result {
    double t = 0; std::vector<Ball> balls; std::vector<PathPt> path;
    struct Ev { double t; int ball; std::string what; }; std::vector<Ev> events;
    // v4: exact vertices of every ball's path (where its direction changes): kind 'W' cushion, 'B' ball-ball contact (other = partner id), 'P' pocket
    struct Vtx { double t; int ball; double x, z; char kind; int other; }; std::vector<Vtx> verts; std::vector<V2> start;
    int wallHits = 0, ballHits = 0, tris = 0; Prediction pred; bool flag108 = false; std::vector<TEv> tev;   // flag108: ServerTracker +0x108 (SphereCast along the shot, P2R)
};

// P2W/P2X: per-table presets. R = 0.5 * ballScale (SphereCollider 0.5 x prefab scale, sharedassets0).
// Y = LOCAL Y of the rack transforms: GameTableSetup.InitializeTableBalls (0x5D62330) spawns every ball at Transform.get_localPosition of RockTransforms/Rock1 (n)
// and OnSpawnBall (0x5D62EBC) only Instantiate(pos,rot) + localScale=ballScale + Spawn (no Y change). All six tables are now sourced (P2X, docs/FINDINGS_P2X.md):
// Level3 root is at world y=2.523 so world rack Y (2.5065) was misleading; local = -0.0165. Level13: root y=0.032, local = -0.0275.
// Rigidbody mass/damping per table: see P2X (read from the game files).
struct TablePreset { const char* key; const char* root; double R, Y; bool ySourced; };
static const TablePreset kTables[] = {
    {"lvl1_4", "TableLvl1-4Model", 0.0400, -0.0165, true}, {"lvl5_6", "TableLvl5&6", 0.0325, -0.0235, true}, {"lvl7_8", "TableLvl7-8", 0.0335, -0.0239, true},
    {"lvl9", "Table-GameLevel9", 0.0275, -0.0296, true}, {"level11", "Table-GameLevel9-12Model", 0.0275, -0.0296, true}, {"lvl13", "Level13Table", 0.0235, -0.0275, true}};
inline const TablePreset* findTable(const std::string& k) { for (auto& t : kTables) if (k == t.key || k == t.root) return &t; return nullptr; }
struct Sim {
    World W; Cfg C; const TablePreset* table = nullptr;
    bool load(const std::string& dir) { return W.load(dir); }
    bool loadTable(const std::string& dir, const std::string& key) {
        const TablePreset* t = findTable(key); if (!t || !W.load(dir, t->key)) return false;
        table = t; C.R = t->R; C.Y = t->Y; return true; }
    bool loadBinary(const unsigned char* d, size_t n) { return W.loadBinary(d, n); }
    // V3_2 merge: which table is on screen?  Every game pocket must sit on a CheckBall box centre of that table (xz, 3 cm) and the ball radius must match (0.6 mm).
    // Returns the kTables key or "" (then do NOT use this simulator).  Reads only <key>_pockets.txt from a text data dir (not the packed binary).
    static std::string detect(const std::string& dir, const std::vector<V2>& pk, double radius) {
        for (const TablePreset& tp : kTables) {
            if (std::fabs(tp.R - radius) > 0.0006) continue;
            FILE* f = fopen((dir + "/" + tp.key + "_pockets.txt").c_str(), "r"); if (!f) continue;
            std::vector<V2> c; char line[512];
            while (fgets(line, 512, f)) { if (line[0] == '#') continue; char n[64]; double v[6];
                if (sscanf(line, "%63s %lf %lf %lf %lf %lf %lf", n, v, v + 1, v + 2, v + 3, v + 4, v + 5) == 7) c.push_back({0.5 * (v[0] + v[3]), 0.5 * (v[2] + v[5])}); }
            fclose(f); if (c.size() != pk.size() || pk.empty()) continue;
            int ok = 0; for (auto& q : pk) for (auto& p : c) if (std::hypot(q.x - p.x, q.z - p.z) < 0.03) { ++ok; break; }
            if (ok == (int)pk.size()) return tp.key;
        }
        return "";
    }

    double wallE(char m) const { return m == 'E' ? C.eEdge : (m == 'K' ? C.ePocket : C.eDefault); }

    // CueBall.OnCastSphere + OnCastCollided + OnHandleCueDirection + PredictedFinalVelocity (client side), on the real table meshes.
    Prediction predict(const std::vector<Ball>& balls, const Shot& s) const {
        Prediction R; const Ball* cue = nullptr; for (auto& b : balls) if (b.id == s.cueId && b.in) cue = &b; if (!cue) return R;
        V2 d = unit({s.dx, s.dz}); if (d.x == 0 && d.z == 0) return R;
        const double maxDist = 6.0; V2 c0{cue->x, cue->z}; double best = 1e18; int kind = 0, tid = -1; V2 nrm; char hm = 0;
        for (auto& b : balls) {                                           // balls: sphere of radius 2R around the other centre
            if (b.id == s.cueId || !b.in) continue; V2 rel = V2{b.x, b.z} - c0; double proj = dot(rel, d); if (proj <= 0) continue;
            double perp2 = dot(rel, rel) - proj * proj, R2 = 4 * C.R * C.R; if (perp2 > R2) continue;
            double t = proj - std::sqrt(R2 - perp2); if (t < best) { best = t; kind = 1; tid = b.id; nrm = unit((c0 + d * t) - V2{b.x, b.z}); }
        }
        WallHit wh = W.sweep(c0, d * maxDist, C.R, C.Y);                    // any solid collider on the way
        if (wh.hit) { double t = wh.t * maxDist; if (t < best) { best = t; kind = 2; nrm = unit({wh.n.x, wh.n.z}); hm = W.T[wh.tri].mat; } }
        int pk; double pt = W.pocketEntry(c0, d * maxDist, C.R, C.Y, pk, C.pocketHull, C.pocketSphere, C.pocketHull3d);   // trigger on the way: tag PocketInside -> the game predicts nothing
        if (pt >= 0 && pt * maxDist < best) { best = pt * maxDist; kind = 3; }
        if (!kind) return R;
        R.hitDistance = best; R.normal = nrm; R.hitMat = hm;
        double F3 = s.F, Fh = s.Fh < 0 ? s.F : s.Fh;
        if (kind == 1) {
            R.valid = true; R.isBall = true; R.targetId = tid;
            V2 rn{-nrm.z, nrm.x};                                           // Euler(0,-90deg,0) * n
            double c = -(dot(d, rn)) / (len(d) * len(rn));
            R.ballAngleDeg = std::acos(game::clampd(c, -1, 1)) * game::kRad2Deg;
            R.angleInt = (int)std::nearbyint(R.ballAngleDeg);               // Mathf.RoundToInt (banker's)
            R.theta = game::cueTheta(R.angleInt);                           // SmoothDamp assumed converged unless thetaFrames > 0
            if (s.thetaFrames > 0 && s.thetaStartDeg >= 0) R.theta = game::thetaAfterFrames(s.thetaStartDeg, R.theta, s.thetaFrames, s.thetaDt);
            R.cueDir = unit(game::rotY(nrm, R.theta));
            if (s.spinAngleDeg != 0) {                                      // P2H: spin quaternion replaces the theta table (PredictCueDirAfterBallCollision)
                cp::V3 o = cp::predictCueDirAfterBallCollision(cp::eulerY((float)(R.theta * game::kDeg2Rad)), (float)s.spinAngleDeg, {(float)nrm.x, 0.f, (float)nrm.z});
                R.cueDir = unit(V2{o.x, o.z});
            }
            R.targetDir = unit(V2{-nrm.x, -nrm.z});
            double dd = F3 - best;
            R.finalVelocity = game::clampd(dd * game::clampd(std::fabs(std::cos(R.ballAngleDeg * game::kDeg2Rad)), game::kMinCueSin, 1.0), 0, C.velClamp);
            R.targetSpeed = std::fabs(std::sin(R.ballAngleDeg * game::kDeg2Rad) * (Fh - game::clampd(best, 0, Fh)));
            double sp = s.spin >= 0 ? s.spin : R.finalVelocity; if (sp <= game::kZeroSpin && s.spinApplied) sp = 0;
            R.cueV = R.cueDir * sp; R.targetV = R.targetDir * R.targetSpeed;
        } else if (kind == 2 && hm == 'E') {                                // tag "Edge"
            R.valid = true; R.isBall = false;
            { V2 rn{-nrm.z, nrm.x};                                          // P2N: OnHandleCueDirection computes intBallAngle for every collider (same formula)
              double c = -(dot(d, rn)) / (len(d) * len(rn));
              R.ballAngleDeg = std::acos(game::clampd(c, -1, 1)) * game::kRad2Deg; R.angleInt = (int)std::nearbyint(R.ballAngleDeg); }
            double k = dot(d, nrm); R.cueDir = unit(V2{d.x - 2 * k * nrm.x, d.z - 2 * k * nrm.z});
            if (s.spinAngleDeg != 0) {                                      // P2H: Edge always applies spin * reflection
                cp::V3 o = cp::predictCueDirAfterEdgeCollision((float)s.spinAngleDeg, {(float)d.x, 0.f, (float)d.z}, {(float)nrm.x, 0.f, (float)nrm.z});
                R.cueDir = unit(V2{o.x, o.z});
            }
            R.finalVelocity = game::clampd(std::fabs(F3 - best), 0, C.velClamp);
            double sp = s.spin >= 0 ? s.spin : R.finalVelocity;             // Edge: no zero-spin rule
            R.cueV = R.cueDir * sp;
        }                                                                   // other tags (K/D/pocket trigger): no prediction
        return R;
    }

    // ServerTracker.OnRequestShotServer flag +0x108 = CueBall.OnCastCueBall (0x5DD6BB8) = Physics.SphereCast(origin = cue position, radius = Ball.radius + 2*defaultContactOffset,
    // dir = normalize(vec), maxDistance = |vec|, layerMask = SpinController.spinLayer +0xF8 = 0xC00 (Cfg.castMask)).  P2R.
    bool castHits(const std::vector<Ball>& balls, int cueId, V2 c0, V2 d, double dist) const {
        const double rc = C.R + 2 * C.contactOffset;
        for (auto& b : balls) {
            if (!((C.castMask >> 10) & 1u)) break;   // balls = layer 10 (BallBase)
            if (b.id == cueId || !b.in) continue; V2 rel = V2{b.x, b.z} - c0; double proj = dot(rel, d); if (proj <= 0 || proj > dist + 2 * C.R + rc) continue;
            double perp2 = dot(rel, rel) - proj * proj, RR = (C.R + rc) * (C.R + rc); if (perp2 > RR) continue;
            if (proj - std::sqrt(RR - perp2) <= dist) return true;
        }
        WallHit wh = W.sweep(c0, d * dist, rc, C.Y, C.castMask); if (wh.hit) return true;
        int pk; if (C.castPockets && W.pocketEntry(c0, d * dist, rc, C.Y, pk, C.pocketHull, C.pocketSphere, C.pocketHull3d) >= 0) return true;
        return false;
    }

    // Run a position/velocity scene.  If `shot` is given, the cue ball gets dir*F first and the game's first-contact override is applied.
    Result run(std::vector<Ball> B, const Shot* shot = nullptr, bool wantPath = false, bool wantEv = false) const {
        Result R; R.tris = (int)W.T.size(); const double r = C.R;
        Prediction pr; bool fcPending = false; int cueId = -1;
        if (shot) {
            pr = predict(B, *shot); R.pred = pr; cueId = shot->cueId;
            V2 d = unit({shot->dx, shot->dz});
            for (auto& b : B) if (b.id == cueId) { b.vx = d.x * shot->F; b.vz = d.z * shot->F; }
            fcPending = C.firstContact;
        }
        if (wantPath) for (auto& b : B) R.start.push_back({b.x, b.z});
        double t = 0; long step = 0; std::vector<char> mv(B.size(), 0);
        auto spd = [&](const Ball& b) { return std::sqrt(b.vx * b.vx + b.vz * b.vz); };
        for (size_t i = 0; i < B.size(); ++i) mv[i] = spd(B[i]) >= C.ballStop;
        auto idx = [&](int id) -> Ball* { for (auto& b : B) if (b.id == id) return &b; return nullptr; };
        // ---- special path state (P2R) ----
        const bool spec = shot && C.special; bool onSim = spec, cueCollided = false, flag108 = false;
        float bx = 0, bz = 0, vcx = 0, vcz = 0;   // baseline (+0xD4) and vec (+0xBC), float32 like the game
        const double fixedF = (double)(float)C.fixedDelta;
        if (spec) {
            if (Ball* cb = idx(cueId)) {
                V2 d = unit({shot->dx, shot->dz}); bx = (float)cb->x; bz = (float)cb->z; vcx = (float)(d.x * shot->F); vcz = (float)(d.z * shot->F);
                flag108 = castHits(B, cueId, {cb->x, cb->z}, d, shot->F);
                R.flag108 = flag108;
            } else onSim = false;
        }
        while (t < C.tmax) {
            double dtc = C.dt; int nSub = C.substeps > 0 ? C.substeps : 1;
            if (spec) {
                dtc = fixedF; nSub = 1;
                Ball* cb = idx(cueId);
                if (onSim && cb && cb->in && !cueCollided) {
                    const float m = std::sqrt(vcx * vcx + vcz * vcz), ux = m > 1e-5f ? vcx / m : 0.f, uz = m > 1e-5f ? vcz / m : 0.f;   // Vector3.Normalized / Magnitude (0x5DC7888 / 0x5DC77C0)
                    const float tx = bx + ux * m, tz = bz + uz * m;
                    cb->vx = (double)tx - cb->x; cb->vz = (double)tz - cb->z;
                    if (flag108) { if (C.substeps > 0) { nSub = C.substeps; dtc = C.dt; } else break; }   // substeps<=0: the game returns without Simulate (stalls)
                    else if (std::sqrt(cb->vx * cb->vx + cb->vz * cb->vz) < C.stop) { cb->x = tx; cb->z = tz; cb->vx = cb->vz = 0; onSim = false; }
                }
            }
            bool moving = false;
            for (auto& b : B) if (b.in && std::sqrt(b.vx * b.vx + b.vz * b.vz) >= C.ballStop) { moving = true; break; }
            if (!moving) break;
            for (int sub = 0; sub < nSub; ++sub) {
            // --- damping application (integration order: damping before collision resolution) ---
            // Linear: v *= max(0, 1 - drag*dt)  [PHYSX_4_1_REFERENCE: bodyCoreComputeUnconstrainedVelocity; NOT confirmed from game binary]
            // Angular: w *= max(0, 1 - angularDrag*dt)  [PHYSX_4_1_REFERENCE; NOT confirmed from game binary]
            double f = C.dragMode == 0 ? std::max(0.0, 1 - C.drag * dtc) : 1.0 / (1 + C.drag * dtc);
            double fa = std::max(0.0, 1 - C.angularDrag * dtc);
            for (auto& b : B) if (b.in) {
                b.vx *= f; b.vz *= f;
                b.aw *= fa;
            }
            double rem = dtc; int ev = 0;
            while (rem > 1e-13 && ev++ < 128) {
                // --- maxDepenetrationVelocity: limit how far any ball moves in one sub-step iteration ---
                // If the fastest ball exceeds maxDepenetrationVelocity, cap the step so its displacement
                // stays <= maxDepenetrationVelocity * dtc. Prevents tunnelling at extreme speeds.
                double step = rem;
                double maxSpeed = 0;
                for (auto& b : B) if (b.in) { double s = std::sqrt(b.vx * b.vx + b.vz * b.vz); if (s > maxSpeed) maxSpeed = s; }
                if (C.maxDepenetrationVelocity > 0 && maxSpeed > C.maxDepenetrationVelocity) {
                    double remCap = C.maxDepenetrationVelocity * dtc / maxSpeed;
                    if (remCap < step) step = remCap;
                }

                double best = step; int kind = 0, ia = -1, ib = -1; WallHit wh; int pk = -1;
                if (C.ccdMode == 0) {
                    // --- CCD mode 0: continuous time-of-impact (analytical sweep, no tunnelling) ---
                    for (size_t i = 0; i < B.size(); ++i) {
                        Ball& a = B[i]; if (!a.in || (a.vx == 0 && a.vz == 0)) continue;
                        V2 c0{a.x, a.z}, disp{a.vx * step, a.vz * step};
                        WallHit h = W.sweep(c0, disp, r, C.Y); if (h.hit && h.t * step < best) { best = h.t * step; kind = 1; ia = (int)i; wh = h; }
                        int w; double pt = W.pocketEntry(c0, disp, r, C.Y, w, C.pocketHull, C.pocketSphere, C.pocketHull3d); if (pt >= 0 && pt * step < best) { best = pt * step; kind = 2; ia = (int)i; pk = w; }
                    }
                } else {
                    // --- CCD mode 1: discrete sub-step (move then check penetration; may tunnel at extreme speeds) ---
                    for (size_t i = 0; i < B.size(); ++i) {
                        Ball& a = B[i]; if (!a.in || (a.vx == 0 && a.vz == 0)) continue;
                        double spd = std::sqrt(a.vx * a.vx + a.vz * a.vz);
                        if (spd < 1e-12) continue;
                        V2 next{a.x + a.vx * step, a.z + a.vz * step};
                        WallHit dh; if (W.discreteWallHit(next, r, C.Y, dh)) { best = step; kind = 1; ia = (int)i; wh = dh; }
                        int w; double pt = W.pocketEntry({a.x, a.z}, {a.vx * step, a.vz * step}, r, C.Y, w, C.pocketHull, C.pocketSphere, C.pocketHull3d); if (pt >= 0 && pt * step < best) { best = pt * step; kind = 2; ia = (int)i; pk = w; }
                    }
                }
                for (size_t i = 0; i < B.size(); ++i) for (size_t j = i + 1; j < B.size(); ++j) {
                    Ball &a = B[i], &b = B[j]; if (!a.in || !b.in) continue;
                    V2 d{b.x - a.x, b.z - a.z}, w{b.vx - a.vx, b.vz - a.vz}; double aa = dot(w, w); if (aa < 1e-18) continue;
                    double bb = dot(d, w); if (bb >= 0) continue; double cc = dot(d, d) - 4 * r * r;
                    double tt; if (cc <= 0) tt = 0; else { double disc = bb * bb - aa * cc; if (disc < 0) continue; tt = (-bb - std::sqrt(disc)) / aa; if (tt < 0) tt = 0; }
                    if (tt < best) { best = tt; kind = 3; ia = (int)i; ib = (int)j; }
                }
                for (auto& b : B) if (b.in) { b.x += b.vx * best; b.z += b.vz * best; }
                rem -= best;
                if (kind == 1) {
                    Ball& a = B[ia]; V3 n = wh.n; double nxz = n.x * n.x + n.z * n.z; double vn = a.vx * n.x + a.vz * n.z;
                    if (vn < 0) {
                        double e = wallE(W.T[wh.tri].mat); double j = -(1 + e) * vn / nxz; a.vx += j * n.x; a.vz += j * n.z; R.wallHits++; if (spec && a.id == cueId) cueCollided = true; if (wantPath) R.verts.push_back({t + (dtc - rem), a.id, a.x, a.z, 'W', -1});
                        // --- PhysX 4.1 wake: any contact wakes a sleeping body (reset wakeCounter) ---
                        if (a.sleeping) { a.sleeping = false; a.wakeCounter = 0; }
                        if (fcPending && a.id == cueId) {
                            fcPending = false;
                            if (pr.valid && !pr.isBall && W.T[wh.tri].mat == 'E') { a.vx = pr.cueV.x; a.vz = pr.cueV.z; }
                        }
                        if (wantEv) R.tev.push_back({1, t + (dtc - rem), ia, -1, {a.x, a.z}, {0, 0}, spd(a), 0, -1, W.T[wh.tri].mat});
                    }
                } else if (kind == 2) {
                    Ball& a = B[ia]; a.in = false; a.pocketed = true; if (spec && a.id == cueId) cueCollided = true; a.tPocket = t + (dtc - rem); a.vx = a.vz = 0;
                    R.events.push_back({a.tPocket, a.id, "pocket " + W.P[pk].name}); if (wantPath) R.verts.push_back({a.tPocket, a.id, a.x, a.z, 'P', pk});
                    if (wantEv) { R.tev.push_back({3, a.tPocket, ia, -1, {a.x, a.z}, {0, 0}, 0, 0, pk, 0}); mv[ia] = 0; }
                } else if (kind == 3) {
                    Ball &a = B[ia], &b = B[ib]; V2 n = unit({b.x - a.x, b.z - a.z}); double vn = (b.vx - a.vx) * n.x + (b.vz - a.vz) * n.z;
                    if (vn < 0) {
                        double j = -(1 + C.eBall) * vn * 0.5; a.vx -= j * n.x; a.vz -= j * n.z; b.vx += j * n.x; b.vz += j * n.z; R.ballHits++; if (spec && (a.id == cueId || b.id == cueId)) cueCollided = true; if (wantPath) { const double te = t + (dtc - rem); R.verts.push_back({te, a.id, a.x, a.z, 'B', b.id}); R.verts.push_back({te, b.id, b.x, b.z, 'B', a.id}); }
                        if (fcPending && (a.id == cueId || b.id == cueId)) {
                            fcPending = false; Ball& tg = (a.id == cueId) ? b : a; Ball& cb = (a.id == cueId) ? a : b;
                            if (pr.valid && pr.isBall && tg.id == pr.targetId) { tg.vx = pr.targetV.x; tg.vz = pr.targetV.z; cb.vx = pr.cueV.x; cb.vz = pr.cueV.z; }
                        }
                        if (wantEv) { R.tev.push_back({2, t + (dtc - rem), ia, ib, {a.x, a.z}, {b.x, b.z}, spd(a), spd(b), -1, 0}); mv[ia] = spd(a) >= C.ballStop; mv[ib] = spd(b) >= C.ballStop; }
                        // --- PhysX 4.1 wake: any contact wakes a sleeping body ---
                        if (a.sleeping) { a.sleeping = false; a.wakeCounter = 0; }
                        if (b.sleeping) { b.sleeping = false; b.wakeCounter = 0; }
                    }
                } else {
                    // no collision this step: best == step (may be capped), rem already decremented; continue loop to consume remaining rem
                }
            }
            t += dtc; ++step;
            if (wantEv) for (size_t i = 0; i < B.size(); ++i) if (B[i].in && mv[i] && !B[i].sleeping && spd(B[i]) < C.bodySleepThreshold) { mv[i] = 0; R.tev.push_back({4, t, (int)i, -1, {B[i].x, B[i].z}, {0, 0}, 0, 0, -1, 0}); }
            // --- PhysX 4.1 sleep / wake model [BINARY: sleep-check@0x13cc6e8 — KE check at 0x13cc8b8 compares 0.5×KE vs wakeCounter×0.4f (BodyCore+0x9C); wakeCounter++ at 0x13cd20c, wakeCounter-- at 0x13cd308] ---
            // Per-body Ball.sleepThreshold = 0.002f is CONFIRMED (dump.cs + runtime dumps); Physics.sleepThreshold = 0.001 from data.unity3d (setSleepThreshold@0x13e0bc8 → BodyCore+0x98).
            // sleepKE=0 (OFF): no binary evidence for a KE threshold; KE threshold in binary uses wakeCounter×0.4f, NOT sleepKE.
            // Wake-up: any non-zero velocity / collision wakes the body (wakeCounter reset).
            if (C.sleepKE > 0) {
                for (auto& b : B) if (b.in && !b.sleeping) {
                    double ke = 0.5 * (b.vx * b.vx + b.vz * b.vz) + 0.5 * b.aw * b.aw;
                    if (ke < C.sleepKE) { b.wakeCounter += dtc; if (b.wakeCounter >= C.wakeCounter) { b.sleeping = true; b.vx = b.vz = 0; b.aw = 0; } }
                    else b.wakeCounter = 0;
                }
            }
            }   // sub-step loop
            if (wantPath) { PathPt p; p.t = t; for (auto& b : B) p.p.push_back({b.x, b.z}); R.path.push_back(p); }
        }
        if (C.solveOverlap && !B.empty()) {   // PhysicsBuilder.SolveBalls: positions only (decoded)
            solver::Params sp; sp.radius = C.R; sp.contactOffset = C.contactOffset; sp.passes = C.solvePasses;
            std::vector<solver::P3> pos; std::vector<bool> tube; int cueIdx = -1;
            for (size_t i = 0; i < B.size(); ++i) { pos.push_back({B[i].x, C.Y, B[i].z}); tube.push_back(!B[i].in); if (B[i].id == cueId) cueIdx = (int)i; }
            pos = solver::solveBalls(pos, tube, cueIdx, false, W.rails, sp);
            for (size_t i = 0; i < B.size(); ++i) if (B[i].in) { B[i].x = pos[i].x; B[i].z = pos[i].z; }
        }
        (void)idx; R.t = t; R.balls = B; return R;
    }
};

}  // namespace pp2
