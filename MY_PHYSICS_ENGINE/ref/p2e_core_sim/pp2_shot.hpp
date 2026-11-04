// PP2 core: the game's first-contact prediction, decoded from libil2cpp.so (this is what the server really applies).
//
// Client, at aim time:  CueBall.OnCastSphere (0x5DD7914)  SphereCast along the shot  ->  hitDistance (RaycastHit.distance), hit.normal, collider
//   -> OnCastCollided (0x5DD7E4C): tag "Ball" -> OnCastBall (0x5DD7FDC) + OnHandleCueDirection;  tag "Edge" -> OnHandleCueDirection;  anything else -> no prediction
//   OnCastBall  : targetBallDirection = PhysicsBuilder.PredictTargetBallAfterCollision (0x5D3CC58).direction = -normalize_xz(hit.normal)   [line of centres]
//   OnHandleCueDirection (0x5DD6EE0), collider is a Ball:
//        ballAngle = acos(clamp(-(u . R n)/(|u||R n|), -1, 1)) in deg, u = stick transform.up (the shot direction), R n = Euler(0,-90deg,0)*n = (-n.z, 0, n.x)
//        a = round-half-even(ballAngle)  -> target rotation theta (deg), CueBall.PredictCueRotationAngelAfterCollision (0x5DD7490):
//             a > 180 : 180 | a < 88 : 95 | a == 88,89 : 150 | a == 90 : 180 | a == 91,92 : 210 | a >= 93 : 265
//        cue.angle (+0x254) = SmoothDamp(current, theta, smoothTime 0.03)  per frame  -> converged when the player actually shoots (assumed here)
//        collisionDirection = PhysicsBuilder.PredictCueDirAfterBallCollision (0x5D420A8) = normalize( Euler(0, angle, 0) * hit.normal )     [no spin; with spin the spin quaternion replaces it]
//      collider is "Edge":  collisionDirection = normalize( PredictCueDirAfterEdgeCollision (0x5D43858) ) = reflect(shotDir, n)  (xz only)
//   PhysicsBuilder.PredictedFinalVelocity (0x5D3DBE0): cue speed, see pp2_game.hpp. SpinController.ApplySpinFactorCombination (0x5D9EF80) starts from it:
//      spinFactor = clamp(finalVelocity + tipOffset*(...), 0, maxSpinValue). With the tip centred (no spin) spinFactor = finalVelocity.   <- spin = phase P2
//   StickService.ShotTheBall (0x5DCAAA4): ShotData{ spinValue = spinFactor, ForceDirection, CollisionDirection = cue +0x220, targetBallDirection = cue +0x22C,
//                                                  targetBallvelocity = PhysicsBuilder +0x44 }
// Server, first contact only (CueBall.OnCollisionEnter 0x5DD9B7C -> SyncSpin -> ApplySpin 0x5DD83B4):
//      target.linearVelocity = normalize_xz(targetBallDirection) * targetBallvelocity           (HandlePhysicsCollisions.SetTargetBallAfterCollision)
//      cue.linearVelocity    = normalize_xz(CollisionDirection) * spinValue                      (SetCueDirectionAfterBallCollide; velocities zeroed first unless collider is "Edge")
#pragma once
#include "pp2_sim.hpp"
#include "pp2_game.hpp"
namespace pp2 { namespace shot {
using sim::V2;

// Target rotation table of PredictCueRotationAngelAfterCollision (degrees about +Y, Unity left-handed rotation).
inline double cueTheta(int a) {
    if (a > 180) return 180.0;
    if (a < 88) return 95.0;
    if (a < 90) return 150.0;
    if (a == 90) return 180.0;
    if (a <= 92) return 210.0;
    return 265.0;
}
// Quaternion.Euler(0, deg, 0) * (x, 0, z)  ->  Unity: x' = x cos + z sin ; z' = -x sin + z cos
inline V2 rotY(V2 v, double deg) { double r = deg * 0.017453292519943295, c = std::cos(r), s = std::sin(r); return {v.x * c + v.z * s, -v.x * s + v.z * c}; }
inline V2 unit(V2 v) { double n = sim::len(v); return n < game::kEps ? V2{0, 0} : V2{v.x / n, v.z / n}; }

struct Input {
    int cueId = 0;
    V2 dir;                     // unit shot direction (xz)
    double F3 = 0;              // |PhysicsBuilder.forceDirection| = initial cue speed
    double Fh = -1;             // PhysicsBuilder.horizontalForce (+0x20); <0 -> same as F3
    double spinValue = -1;      // SpinController.SpinFactor; <0 -> centred tip (= finalVelocity). Spin itself is phase P2.
    bool spinApplied = false;
};
struct Prediction {
    bool valid = false;         // the cast hit a "Ball" or an "Edge" -> the game predicts something
    bool isBall = false;
    int targetId = -1;
    double hitDistance = 0, ballAngleDeg = 0, theta = 0, finalVelocity = 0, targetSpeed = 0;
    int angleInt = 0;
    V2 normal, cueDir, targetDir, cueV, targetV;
    sim::FirstContact fc;
};

// The SphereCast of CueBall.OnCastSphere on this table model: nearest of {other balls (radius 2r around the centre), cushion planes for the ball CENTRE}.
inline Prediction predict(const sim::Table& tb, const sim::Params& P, const std::vector<sim::Ball>& balls, const Input& in) {
    Prediction R; const sim::Ball* cue = nullptr;
    for (auto& b : balls) if (b.id == in.cueId && b.alive) cue = &b;
    if (!cue) return R;
    V2 d = unit(in.dir); if (d.x == 0 && d.z == 0) return R;
    double best = 1e18; int kind = 0; int tid = -1; V2 nrm;
    for (auto& b : balls) {                                    // balls
        if (b.id == in.cueId || !b.alive) continue;
        V2 rel = b.p - cue->p; double proj = sim::dot(rel, d); if (proj <= 0) continue;
        double perp2 = sim::dot(rel, rel) - proj * proj, R2 = 4 * P.ballR * P.ballR; if (perp2 > R2) continue;
        double t = proj - std::sqrt(R2 - perp2);
        if (t < best) { best = t; kind = 1; tid = b.id; V2 c = cue->p + d * t; nrm = unit(c - b.p); }
    }
    auto wall = [&](double dv, double lim, double pv, V2 n) { if (dv == 0) return; double t = (lim - pv) / dv; if (t >= 0 && t < best) { best = t; kind = 2; tid = -1; nrm = n; } };
    wall(d.x, d.x > 0 ? tb.xmax : tb.xmin, cue->p.x, V2{d.x > 0 ? -1.0 : 1.0, 0});
    wall(d.z, d.z > 0 ? tb.zmax : tb.zmin, cue->p.z, V2{0, d.z > 0 ? -1.0 : 1.0});
    if (!kind) return R;
    R.valid = true; R.isBall = (kind == 1); R.targetId = tid; R.hitDistance = best; R.normal = nrm;
    double Fh = in.Fh < 0 ? in.F3 : in.Fh;
    if (R.isBall) {
        V2 rn = {-nrm.z, nrm.x};                                                       // Euler(0,-90deg,0) * n
        double c = -(sim::dot(d, rn)) / (sim::len(d) * sim::len(rn));
        R.ballAngleDeg = std::acos(game::clampd(c, -1.0, 1.0)) * 57.29578;                  // rad->deg constant read from libil2cpp rodata (0x13190E4)
        R.angleInt = (int)std::nearbyint(R.ballAngleDeg);                              // Mathf.RoundToInt (banker's rounding)
        R.theta = cueTheta(R.angleInt);
        R.cueDir = unit(rotY(nrm, R.theta));
        R.targetDir = {-unit(nrm).x, -unit(nrm).z};
        R.finalVelocity = game::predictedFinalVelocity(in.F3, best, R.ballAngleDeg, false);
        R.targetSpeed = game::targetBallFinalVelocity(Fh, best, R.ballAngleDeg);
    } else {
        double k = sim::dot(d, nrm); R.cueDir = unit(V2{d.x - 2 * k * nrm.x, d.z - 2 * k * nrm.z});
        R.finalVelocity = game::predictedFinalVelocity(in.F3, best, 0, true);
    }
    double s = in.spinValue >= 0 ? in.spinValue : R.finalVelocity;
    if (R.isBall && s <= game::kZeroSpin && in.spinApplied) s = 0;                      // SetCueDirectionAfterBallCollide zero-spin rule
    R.cueV = R.cueDir * s; R.targetV = R.targetDir * R.targetSpeed;
    R.fc.enabled = true; R.fc.cueId = in.cueId; R.fc.targetId = tid; R.fc.edgeNormal = nrm; R.fc.cueV = R.cueV; R.fc.targetV = R.targetV;
    return R;
}

// One call: predict + simulate a shot exactly like the game does (initial cue velocity = dir*F3, first-contact override, then physics).
inline sim::Result shoot(const sim::Table& tb, const sim::Params& P, std::vector<sim::Ball> balls, const Input& in, Prediction* outPred = nullptr, sim::Trace* tr = nullptr) {
    Prediction pr = predict(tb, P, balls, in);
    for (auto& b : balls) if (b.id == in.cueId) b.v = unit(in.dir) * in.F3;
    if (outPred) *outPred = pr;
    return sim::run(tb, P, balls, tr, pr.valid ? &pr.fc : nullptr);
}
}}
