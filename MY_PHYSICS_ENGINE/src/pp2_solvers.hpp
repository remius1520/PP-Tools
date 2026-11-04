// Position (overlap) solvers of the game, reconstructed from ARM64 (libil2cpp.so).  Pure functions, no engine dependency.
//   PhysicsBuilder.SolveBalls             0x5D40FA4   one pass: per ball -> SolveWithCueBall -> SolveBallWithCushions; then SolveBallWithBall
//   PhysicsBuilder.SolveWithCueBall       0x5D3F86C
//   PhysicsBuilder.SolveBallWithCushions  0x5D3FB10
//   PhysicsBuilder.SolveBallWithBall      0x5D3FE64
// Evidence: (A) read from disassembly, (C) value outside the binary kept as explicit parameter, (M) from MASTER_8, NOT re-verified here.
// They only move POSITIONS (Dictionary<Ball,Vector3> newPos); velocities are never touched.
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace pp2 { namespace solver {

struct P3 { double x = 0, y = 0, z = 0; };
inline P3 operator+(P3 a, P3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline P3 operator-(P3 a, P3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline P3 operator*(P3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double len3(P3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

struct Aabb { P3 lo, hi; };

struct Params {
    double radius = 0.0275;       // Ball.radius (+0xB4) of the cue ball.  (C) prefab value not read; world radius assumed (gap = dist - 2r only makes sense for that)
    double contactOffset = 0.001; // Physics.defaultContactOffset: PhysicsManager 0.001 (game file)
    // (A) float literals from libil2cpp .rodata:
    double safeval = 0.00011999999696854502;   // 0x1319B38 = 1.2e-4        (SolveBalls: s8, passed as `safeval`)
    double edgeFactor = 0.6000000238418579;    // 0x1318940 = 0.6           (edgeSolver = contactOffset * 0.6)
    double minLen = 9.999999747378752e-06;     // 0x1319D60 = 1e-5          (normalise guard)
    P3 fallbackDir;                            // (C) static Vector3 at [class+0xB8], used when the normalise guard fails. Unknown -> zero
    int passes = 8;                            // (M) solvecount = 8, loop is in the caller (not found as a direct bl to 0x5D40FA4)
    // (A) solverValue = 2*contactOffset + safeval ; edgeSolver = contactOffset * edgeFactor
    double solverValue() const { return 2.0 * contactOffset + safeval; }
    double edgeSolver() const { return contactOffset * edgeFactor; }
};

inline P3 dirOrFallback(P3 d, double dist, const Params& p) { return dist > p.minLen ? d * (1.0 / dist) : p.fallbackDir; }

// 0x5D3F86C.  ball != cue && !foul.  cuePos = cueBall.transform.position (NOT newPos).
// gap = |cue - pos| - 2r ; gap > solverValue -> unchanged.
// push = (gap == solverValue ? safeval : 0) + solverValue - gap  along (pos - cue)/|pos - cue|.
inline P3 solveWithCueBall(P3 pos, P3 cuePos, bool ballIsCue, bool foul, const Params& p) {
    if (ballIsCue || foul) return pos;
    double dist = len3(cuePos - pos);                     // note: the binary's order is (cue - pos); length is the same
    double gap = dist - 2.0 * p.radius, sv = p.solverValue();
    if (gap > sv) return pos;
    double base = (gap == sv) ? p.safeval : 0.0;
    P3 dir = dirOrFallback(pos - cuePos, len3(pos - cuePos), p);
    return pos + dir * (base + sv - gap);
}

// Collider.ClosestPointOnBounds on each cushion collider's AABB, in list order, pos updated between colliders.  0x5D3FB10
// gap = |pos - closest| - r (r = cueBall.radius) ; gap > edgeSolver -> skip ; push = (gap==edgeSolver?safeval:0) + edgeSolver - gap
// along (pos - closest)/dist.  Applied to EVERY ball including the cue (the function has no ball != cue test).
inline P3 closestOnBounds(P3 q, const Aabb& b) {
    return {std::min(std::max(q.x, b.lo.x), b.hi.x), std::min(std::max(q.y, b.lo.y), b.hi.y), std::min(std::max(q.z, b.lo.z), b.hi.z)};
}
inline P3 solveBallWithCushions(P3 pos, const std::vector<Aabb>& edges, const Params& p) {
    double es = p.edgeSolver();
    for (const Aabb& e : edges) {
        P3 c = closestOnBounds(pos, e), d = pos - c; double dist = len3(d), gap = dist - p.radius;
        if (gap > es) continue;
        double base = (gap == es) ? p.safeval : 0.0;
        pos = pos + dirOrFallback(d, dist, p) * (base + es - gap);
    }
    return pos;
}

// 0x5D3FE64.  All pairs i<j of balls that are not InTube; positions re-read from newPos for every pair.
// dist >= 2r -> skip.  dist < minLen -> direction = Random.insideUnitSphere (normalised) : non-deterministic, caller supplies `coincidentDir`.
// else dir = d/dist.  Each ball moves by (2r - dist) * 0.5 along dir (A gets -dir, B gets +dir).
struct PairHook { P3 coincidentDir; };
inline void solveBallWithBall(std::vector<P3>& pos, const std::vector<bool>& inTube, const Params& p, const PairHook& hook = {}) {
    size_t n = pos.size(); double D = 2.0 * p.radius;
    for (size_t i = 0; i < n; ++i) {
        if (inTube[i]) continue;
        for (size_t j = i + 1; j < n; ++j) {
            if (inTube[j]) continue;
            P3 d = pos[j] - pos[i]; double dist = len3(d);
            if (!(dist < D)) continue;                     // b.pl: dist >= D (or NaN) skips
            P3 dir;
            if (dist >= p.minLen) dir = d * (1.0 / dist);
            else { double l = len3(hook.coincidentDir); dir = l > p.minLen ? hook.coincidentDir * (1.0 / l) : p.fallbackDir; }
            double sh = (D - dist) * 0.5;
            pos[i] = pos[i] - dir * sh; pos[j] = pos[j] + dir * sh;
        }
    }
}

// One call of PhysicsBuilder.SolveBalls: returns newPos.  cueIndex = index of the cue ball in `pos`.
inline std::vector<P3> solveBallsOnce(const std::vector<P3>& pos, const std::vector<bool>& inTube, int cueIndex, bool foul,
                                      const std::vector<Aabb>& edges, const Params& p, const PairHook& hook = {}) {
    std::vector<P3> np = pos;
    bool haveCue = cueIndex >= 0 && cueIndex < (int)pos.size();   // the game always has a cue ball; without one the cue step is skipped
    P3 cuePos = haveCue ? pos[cueIndex] : P3{};
    for (size_t i = 0; i < pos.size(); ++i) {
        if (inTube[i]) continue;
        P3 q = haveCue ? solveWithCueBall(pos[i], cuePos, (int)i == cueIndex, foul, p) : pos[i];
        q = solveBallWithCushions(q, edges, p);
        np[i] = q;
    }
    solveBallWithBall(np, inTube, p, hook);
    return np;
}

// Caller loop (M): `passes` times, each pass feeds the previous result back.
inline std::vector<P3> solveBalls(std::vector<P3> pos, const std::vector<bool>& inTube, int cueIndex, bool foul,
                                  const std::vector<Aabb>& edges, const Params& p, const PairHook& hook = {}) {
    for (int k = 0; k < p.passes; ++k) pos = solveBallsOnce(pos, inTube, cueIndex, foul, edges, p, hook);
    return pos;
}

}}  // namespace pp2::solver
