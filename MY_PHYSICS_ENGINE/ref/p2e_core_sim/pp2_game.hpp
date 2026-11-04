// PP2 core: the game's OWN shot formulas (client PhysicsBuilder + server HandlePhysicsCollisions), decoded from libil2cpp.so.
// Source: PP2_GAME_VALUES_FINAL.md. [M] = confirmed in code, [R] = probable, still to be confirmed at run time.
// The game does NOT rely on PhysX restitution for the first contact: the client predicts, the server applies set_linearVelocity (no AddForce anywhere).
#pragma once
#include <cmath>
#include <algorithm>
namespace pp2 { namespace game {

constexpr double kVelClamp = 20.0, kBallKeep = 0.92, kEdgeKeep = 0.92, kZeroSpin = 0.02, kDeg2Rad = 0.0174533, kRad2Deg = 57.2958, kEps = 1e-5;

struct V2 { double x = 0, z = 0; };
inline double len(V2 a) { return std::hypot(a.x, a.z); }
inline V2 normalize(V2 a) { double n = len(a); return n < kEps ? V2{0, 0} : V2{a.x / n, a.z / n}; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

// [M] RESOLVED from libil2cpp.so: the PLT slots are libc imports (readelf -r): GOT +0x4B8 = cosf, +0x4C0 = sinf.
//   PredictedFinalVelocity (cue ball)      calls the +0x4B8 stub  -> cosf(angle * 0.0174533)   (Tb = cos)
//   PredictTargetBallFinalVelocity (object) calls the +0x4C0 stub -> sinf(angle * 0.0174533)   (Ta = sin)
// angle = CueBall.ballAngle in DEGREES = acos(-(stickUp . R90*hitNormal)) * 57.2958 (CueBall.OnHandleCueDirection), i.e. 90deg - cut angle.
// (An earlier note guessed the opposite assignment; it was wrong.) Still function pointers so a device check can override.
struct Trig {
    double (*ta)(double angleDeg) = [](double a) { return std::sin(a * kDeg2Rad); };
    double (*tb)(double angleDeg) = [](double a) { return std::cos(a * kDeg2Rad); };
    double minCueAngleRadius = 0.04;  // PhysicsBuilder.minCueAngelRadiuse (+0x4C), level5 value 0.04 [R strong: the float right after levelMaxPower=4.3]
};

// PhysicsBuilder.PredictedFinalVelocity [M]: cue-ball speed at contact. F = |forceDirection| (3D length of the vector at +0x28);
// specialTag = collider tag a.dx() = "Edge" (decoded from the obfuscated string table, see a_strings decode in HANDOFF_P1C).
// clamp order in the code: |tb| clamped to [min,1] with min winning, then (F-d)*factor clamped to [0,20].
inline double predictedFinalVelocity(double F, double hitDistance, double angleDeg, bool specialTag, const Trig& T = Trig()) {
    double d = F - hitDistance;
    double f = specialTag ? std::fabs(d) : d * clampd(std::fabs(T.tb(angleDeg)), T.minCueAngleRadius, 1.0);
    return clampd(f, 0.0, kVelClamp);
}
// PhysicsBuilder.PredictTargetBallFinalVelocity [M]. NOTE: F here is `horizontalForce` (+0x20), NOT |forceDirection| (they can differ).
inline double targetBallFinalVelocity(double F, double hitDistance, double angleDeg, const Trig& T = Trig()) {
    return std::fabs(T.ta(angleDeg) * (F - clampd(hitDistance, 0.0, F)));
}
// Server: SetTargetBallAfterCollision [M] (y = 0)
inline V2 serverTargetVelocity(V2 targetBallDirection, double targetBallVelocity) { V2 d = normalize(targetBallDirection); return {d.x * targetBallVelocity, d.z * targetBallVelocity}; }
// Server: SetCueDirectionAfterBallCollide [M]. excludedTag = collider has the tag in a static string (name NOT resolved yet) -> nothing changes, returns false.
// collider tag "Edge": the zero-spin rule is skipped and the velocities are not zeroed first (the velocity is simply set to dir*spin).
// otherwise: spin<=zeroSpinThreshold(0.02) && SpinApplied -> 0; linear+angular velocity zeroed, then linear = normalize_xz(dir)*s.
inline bool serverCueVelocity(V2 collisionDirection, double spinValue, bool spinApplied, bool excludedTag, V2& out, bool colliderIsEdge = false) {
    if (excludedTag) return false;
    double s = (!colliderIsEdge && spinValue <= kZeroSpin && spinApplied) ? 0.0 : spinValue;
    V2 d = normalize(collisionDirection); out = {d.x * s, d.z * s}; return true;
}
// WHEN the formulas run [M] (CueBall.OnCollisionEnter 0x5DD9B7C -> SyncSpin 0x5DD8270 -> ApplySpin 0x5DD83B4):
//  * server only (NetworkBehaviour.IsServer @0x3A) and gameManager+0xAC flag set;
//  * collided object does NOT have tag "Table"; the CueBall `collided` flag (@0x26C) is still false -> it is set, so only the FIRST contact counts;
//  * that contact's GameObject must equal the predicted collidedBall (@0x218), and the server-side "spin applied" flag (+0x168) must be unset (set afterwards) -> once per shot;
//  * ApplySpin: SetTargetBallAfterCollision(collided ball, serverShotData.targetBallvelocity(+0xEC), targetBallDirection(+0xE0)) and
//    SetCueDirectionAfterBallCollide(CollisionDirection(+0xC8), spinValue(+0xB8), collided collider, cue rigidbody). Both overwrite velocities AFTER PhysX solved the contact.
// Every later contact (cue vs 2nd ball, ball vs ball, cushions) is plain PhysX with the materials/damping of GAME_PHYSICS_FACTS.
inline bool formulaApplies(bool isServer, bool shotActive, bool tagTable, bool& collidedFlag, bool isPredictedBall, bool& spinAppliedFlag) {
    if (!isServer || !shotActive || tagTable || collidedFlag) return false;
    collidedFlag = true;
    if (!isPredictedBall || spinAppliedFlag) return false;
    spinAppliedFlag = true; return true;
}
// SolveCollisions / BallToEdgeCollisionDir [M]: v' = (v - 2(v.n)n) * 0.92, n = unit normal
inline V2 edgeBounce(V2 v, V2 n) { double k = v.x * n.x + v.z * n.z; return {(v.x - 2 * k * n.x) * kEdgeKeep, (v.z - 2 * k * n.z) * kEdgeKeep}; }
// SolveCollisions ball x ball [M]: angle = acos(clamp(v.u/(|v||u|),-1,1)) in degrees -> BallToBallCollisionDir
inline V2 ballToBall(V2 v, V2 u, V2 hitNormal, double hitDistance, const Trig& T = Trig()) {
    double nv = len(v), nu = len(u); double c = (nv * nu > 0) ? (v.x * u.x + v.z * u.z) / (nv * nu) : 1.0;
    double ang = std::acos(clampd(c, -1.0, 1.0)) * kRad2Deg;
    V2 d = normalize(hitNormal); d = {-d.x, -d.z};
    double rem = nv - clampd(hitDistance, 0.0, nv);
    double m = std::fabs(T.ta(ang) * rem) * kBallKeep; return {d.x * m, d.z * m};
}

// [M] PhysicsBuilder.GetStickForwardCastDireciton (0x5D3DAC8, P2D): forceDirection (+0x28, y forced to 0) from the stick's transform.up:
//   d = up / |up|_3D  (3-D length, so a raised cue shortens the horizontal part),  forceDirection = (d.x*F, 0, d.z*F),  F = horizontalForce (+0x20).
//   If |up| <= 1e-5 the code falls back to another object's (x,z) direction (not modelled: returns 0). F itself is NOT computed in code:
//   horizontalForce has setters (UpdateHorizontalForce 0x5D40C90, set_HorizontalForce, lqv) with no direct callers = driven by a UnityEvent/UI slider -> read it at runtime.
//   => u = stick.transform.up IS the shot direction (forceDirection points along +up), confirming the P1C assumption.
struct FDir { V2 d; double mag; bool ok = false; };
inline FDir forceDirectionFromStickUp(double ux, double uy, double uz, double F) {
    FDir r; double L = std::sqrt(ux * ux + uy * uy + uz * uz); if (!(L > 1e-5)) return r;
    r.d = {ux / L * F, uz / L * F}; r.mag = len(r.d); r.ok = true; return r;      // mag = |forceDirection| used by predictedFinalVelocity
}

}}
